#include "state_storage.h"

#include <util/generic/size_literals.h>
#include <util/stream/file.h>
#include <util/string/cast.h>
#include <util/string/split.h>
#include <util/system/file_lock.h>
#include <util/system/getpid.h>

using namespace NThreading;

namespace NUnifiedAgent {
    namespace {
        class TStateStorage: public IStateStorage {
        public:
            explicit TStateStorage(const TConfig& config, TFlowPluginContext& ctx)
                : Config(config)
                , DirectoryLock(IStateDirectoryLock::Make(Config.Directory(), ctx))
                , CheckpointLock()
                , Lock()
                , UnstableFile(nullptr)
                , LastState(Nothing())
                  , Logger(ctx.Logger())
            {
            }

            void Start() override {
                Config.Directory().MkDirs();
                DirectoryLock->Acquire();
            }

            TFuture<void> Stop() override {
                return MakeFuture();
            }

            void Save(const TString& state) override {
                Y_UNUSED(state);
            }

            TString Load() override {
                return "";
            }

        private:
            struct TStateFile: public TAtomicRefCount<TStateFile> {
                TFileAccessor Accessor;
                ui64 Index;
            };

        private:
            const TConfig& Config;
            THolder<IStateDirectoryLock> DirectoryLock;
            TMutex CheckpointLock;
            TAdaptiveLock Lock;
            TIntrusivePtr<TStateFile> UnstableFile;
            TFMaybe<TString> LastState;
            TScopeLogger& Logger;
        };

        class TStateDirectoryLock: public IStateDirectoryLock {
        public:
            explicit TStateDirectoryLock(const TFsPath& directory, TFlowPluginContext& ctx)
                : Directory(directory)
                , Ctx(ctx)
                , Logger(Ctx.Logger())
                , LockFilePath(directory / "lock")
                , RunFilePath(directory / "run")
                , PrevRunFilePath(directory / "run.prev")
                , LockFile(Nothing())
                , FileLockAcquired(false)
                , RunFileOwned(false)
                , WasGracefulShutdown_(false)
            {
            }

            void Acquire() override {
                LockFile.ConstructInPlace(LockFilePath);
                Y_ENSURE(LockFile->TryAcquire(),
                    Sprintf("can't lock directory [%s], maybe other instance is running there, "
                            "run file [%s], prev run file [%s]",
                        Directory.c_str(),
                        ReadRunFile(false).c_str(),
                        ReadRunFile(true).c_str()));
                FileLockAcquired = true;
                WasGracefulShutdown_ = !RunFilePath.Exists();
                const auto runFileContent = ReadRunFile(false);
                const auto prevRunFileContent = ReadRunFile(true);
                YLOG(WasGracefulShutdown_ ? TLOG_INFO : TLOG_NOTICE,
                    Sprintf("state directory [%s] lock acquired, was graceful shutdown [%d], "
                            "run file [%s], prev run file [%s]",
                        Directory.c_str(),
                        WasGracefulShutdown_,
                        runFileContent.c_str(),
                        prevRunFileContent.c_str()),
                    Logger);
                WriteRunFile(runFileContent, prevRunFileContent);
                RunFileOwned = true;
                TFileUtils::FlushDirectory(LockFilePath.Dirname());
            }

            void Release() override {
                if (RunFileOwned) {
                    RunFilePath.RenameTo(PrevRunFilePath);
                }
                if (LockFile.Defined() && FileLockAcquired) {
                    LockFile->Release();
                    LockFile.Clear();
                }
            }

            bool WasGracefulShutdown() override {
                return WasGracefulShutdown_;
            }

        private:
            TString ReadRunFile(bool prev) {
                try {
                    return TFileInput(prev ? PrevRunFilePath : RunFilePath).ReadAll();
                } catch(...) {
                    return "";
                }
            }

            void WriteRunFile(const TString& runFileContent, const TString& prevRunFileContent) {
                auto tryParseRunId = [](const TString& s) -> TFMaybe<ui64> {
                    const auto items = StringSplitter(s).Split(';').ToList<TString>();
                    if (items.size() != 4) {
                        return Nothing();
                    }
                    ui64 result{};
                    return TryFromString<ui64>(items[3], result) ? MakeFMaybe(result) : Nothing();
                };
                auto runId = tryParseRunId(runFileContent);
                if (!runId.Defined()) {
                    runId = tryParseRunId(prevRunFileContent);
                }
                const auto content = Sprintf("%d;%s;%s;%lu",
                    GetPID(),
                    Ctx.CommandLine().FormatAsString().c_str(),
                    Ctx.Id().c_str(),
                    runId.GetOrElse(0) + 1);
                TFileAccessor fileAccessor(RunFilePath, CreateAlways | WrOnly);
                fileAccessor.Write(content.c_str(), content.size());
            }

        private:
            const TFsPath Directory;
            TFlowPluginContext& Ctx;
            TScopeLogger& Logger;
            const TFsPath LockFilePath;
            const TFsPath RunFilePath;
            const TFsPath PrevRunFilePath;
            TFMaybe<TFileLock> LockFile;
            bool FileLockAcquired;
            bool RunFileOwned;
            bool WasGracefulShutdown_;
        };
    }

    IStateStorage::TStateFileReader::TStateFileReader(ui64 fileIndex,
        const TString& filePath,
        TScopeLogger& logger)
        : TStateFileReader(fileIndex, TFileReader(filePath, 50_KB), logger)
    {
    }

    IStateStorage::TStateFileReader::TStateFileReader(ui64 fileIndex,
        TFileReader&& fileReader,
        TScopeLogger& logger)
        : FileIndex_(fileIndex)
        , FileReader(std::move(fileReader))
        , Logger(logger)
        , Header()
        , LastBlockSize_(0)
        , LastBlockData_()
        , Offset(0)
        , State(EState::WaitHeader)
    {
        MoveFirst();
    }

    void IStateStorage::TStateFileReader::MoveFirst() {
        SetOffset(sizeof(TFileRecordHeader));
    }

    void IStateStorage::TStateFileReader::SetOffset(ui64 offset) {
        FileReader.Reset(offset);
        Offset = offset;
        State = EState::WaitHeader;
    }

    void IStateStorage::TStateFileReader::ReadNext() {
        if (State == EState::Corrupted) {
            Y_FAIL("reader is in corrupted state");
        } else if (State == EState::Value) {
            State = EState::WaitHeader;
        }
        if (State == EState::WaitHeader) {
            if (!FileReader.Read(Header)) {
                return;
            }
            if (Header.Magic != TFileHeader::ValidMagic) {
                YLOG_ERR(Sprintf("state file [%s], invalid magic", FileReader.FilePath().c_str()));
                State = EState::Corrupted;
                return;
            }
            if (ComputeCrc(Header) != Header.Crc) {
                YLOG_ERR(Sprintf("state file [%s], header crc mismatch", FileReader.FilePath().c_str()));
                State = EState::Corrupted;
                return;
            }
            State = EState::WaitBlock;
        }
        const auto blockSize = static_cast<size_t>(Header.BlockSize);
        const auto* data = FileReader.Read(blockSize);
        if (!data) {
            return;
        }
        const auto blockCrc = Crc32c(data, blockSize);
        if (Header.BlockCrc != blockCrc) {
            YLOG_ERR(Sprintf("state file [%s], block crc mismatch", FileReader.FilePath().c_str()));
            State = EState::Corrupted;
            return;
        }
        LastBlockSize_ = sizeof(Header) + blockSize;
        LastBlockData_ = TStringBuf(data, blockSize);
        Offset += LastBlockSize_;
        State = EState::Value;
    }

    IStateStorage::TStateFileWriter::TStateFileWriter(const TFsPath& directory, ui64 index)
        : File(StateFilePath(directory, index), OpenAlways | WrOnly | Seq | ::ForAppend)
        , FileIndex_(index)
        , Lock()
        , LastRevision(0)
    {
        TFileHeader header;
        TFileHeader::PrepareHeader(header);
        File.Write(&header, sizeof(header));
    }

    IStateStorage::TStateFileWriter::~TStateFileWriter() = default;

    void IStateStorage::TStateFileWriter::Write(const TBuffer& record, ui64 revision) {
        with_lock(Lock) {
            Y_VERIFY(revision != LastRevision);
            if (revision < LastRevision) {
                return;
            }
            File.Write(record.Data(), record.Size());
            LastRevision = revision;
        }
    }

    THolder<IStateDirectoryLock> IStateDirectoryLock::Make(const TFsPath& directory, TFlowPluginContext& ctx) {
        return MakeHolder<TStateDirectoryLock>(directory, ctx);
    }

    TFileProtoStorage::TFileProtoStorage(const TFsPath& directory, TScopeLogger& logger)
        : FilePath(directory / "data")
        , File(nullptr)
        , Logger(logger.Child("proto_storage"))
    {
        directory.MkDirs();
    }

    bool TFileProtoStorage::TryLoad(google::protobuf::Message& target) const {
        if (!FilePath.Exists()) {
            YLOG_INFO(Sprintf("file [%s] not found", FilePath.c_str()));
            return false;
        }
        const auto data = TFileInput(FilePath).ReadAll();
        if (data.Size() <= HeaderSize) {
            YLOG_ERR(Sprintf("file [%s] has unexpected size [%lu]",
                FilePath.c_str(), data.Size()));
            return false;
        }
        ui32 expectedCrc;
        memcpy(&expectedCrc, data.Data(), CrcSize);
        const auto actualCrc = Crc32c(data.data() + CrcSize, data.Size() - CrcSize);
        if (actualCrc != expectedCrc) {
            YLOG_ERR(Sprintf("file [%s] crc mismatch, expected [%u], actual [%u]",
                FilePath.c_str(), expectedCrc, actualCrc));
            return false;
        }
        size_t protoSize;
        memcpy(&protoSize, data.Data() + CrcSize, SizeSize);
        const auto* protoBytes = data.data() + HeaderSize;
        if (!target.ParseFromArray(protoBytes, static_cast<int>(protoSize))) {
            YLOG_ERR(Sprintf("can't parse proto from file [%s]",
                FilePath.c_str()));
            return false;
        }
        YLOG_INFO(Sprintf("loaded proto [%s]", target.ShortDebugString().c_str()));
        return true;
    }

    void TFileProtoStorage::Save(const google::protobuf::Message& source) {
        const auto protoSize = source.ByteSizeLong();
        TBuffer buffer(protoSize + HeaderSize);
        Y_VERIFY(source.SerializeToArray(buffer.Data() + HeaderSize,
            buffer.Capacity() - HeaderSize));
        memcpy(buffer.Data() + CrcSize, &protoSize, SizeSize);
        const auto crc = Crc32c(buffer.Data() + CrcSize, buffer.Capacity() - CrcSize);
        memcpy(buffer.Data(), &crc, CrcSize);

        if (File == nullptr) {
            File = MakeHolder<TFileAccessor>(FilePath.GetPath(), OpenAlways);
        }
        File->Pwrite(buffer.Data(), buffer.Capacity(), 0);
        YLOG_DEBUG(Sprintf("saved proto [%s]", source.ShortDebugString().c_str()));
    }
}
