#pragma once

#include <logbroker/unified_agent/common/binary_file_formats.h>
#include <logbroker/unified_agent/common/config_node.h>
#include <logbroker/unified_agent/common/file_utils.h>
#include <logbroker/unified_agent/interface/flow_plugin.h>

#include <google/protobuf/message.h>
#include <library/cpp/threading/future/future.h>

namespace NUnifiedAgent {
    class IStateStorage {
    private:
        struct TFileHeader: public ::NUnifiedAgent::TFileHeader {
        };
        static_assert(sizeof(TFileHeader) == 16,
            "invalid size of TFileHeader, expected 16");

        struct TFileRecordHeader {
            ui32 Crc;
            ui32 Magic;
            ui32 BlockCrc;
            ui32 BlockSize;
        };
        static_assert(sizeof(TFileRecordHeader) == 16,
            "invalid size of TFileRecordHeader, expected 16");

    public:
        struct TConfig: public TConfigNode  {
            CONFIG_FIELD(TFsPath, Directory);

            CONFIG_FIELD(TFMaybe<TDuration>, CheckpointPeriod,
                .Optional(TDuration::Seconds(1)));
        };

        class TStateFileReader {
        public:
            TStateFileReader(ui64 fileIndex, const TString& filePath, TScopeLogger& logger);

            TStateFileReader(ui64 fileIndex, TFileReader&& fileReader, TScopeLogger& logger);

            inline const TString& FilePath() const noexcept {
                return FileReader.FilePath();
            }

            inline ui64 LastBlockSize() const {
                return LastBlockSize_;
            }

            inline TStringBuf LastBlockData() const {
                return LastBlockData_;
            }

            void SetOffset(ui64 offset);

            inline ui64 FileIndex() const noexcept {
                return FileIndex_;
            }

            inline bool IsStableFile() const noexcept {
                return StateFileIsStable(FileIndex_);
            }

            inline bool Exhausted() const noexcept {
                return Offset == FileReader.GetSize();
            }

            inline bool Corrupted() const noexcept {
                return State == EState::Corrupted;
            }

            inline bool HasValue() const noexcept {
                return State == EState::Value;
            }

            inline ui64 GetOffset() const noexcept {
                return Offset;
            }

            void MoveFirst();

            void ReadNext();

        private:
            enum class EState {
                WaitHeader,
                WaitBlock,
                Corrupted,
                Value
            };

        private:
            ui64 FileIndex_;
            TFileReader FileReader;
            TScopeLogger& Logger;
            TFileRecordHeader Header;
            ui64 LastBlockSize_;
            TStringBuf LastBlockData_;
            ui64 Offset;
            EState State;
        };

        class TStateFileWriter final: public TAtomicRefCount<TStateFileWriter> {
        public:
            TStateFileWriter(const TFsPath& directory, ui64 index);

            ~TStateFileWriter();

            ui64 FileIndex() const {
                return FileIndex_;
            }

            TFsPath FilePath() const {
                return File.FilePath();
            }

            void Write(const TBuffer& record, ui64 revision);

            void Flush() {
                File.Flush();
            }

        private:
            TFileAccessor File;
            ui64 FileIndex_;
            TMutex Lock;
            ui64 LastRevision;
        };

    public:
        static inline bool StateFileIsStable(ui64 fileIndex) {
            return (fileIndex & 1) == 0;
        }

        static inline TFsPath StateFilePath(const TFsPath& directory, ui64 index) {
            return directory / ToString(index);
        }

    public:
        virtual ~IStateStorage() = default;

        virtual void Start() = 0;

        virtual NThreading::TFuture<void> Stop() = 0;

        virtual void Save(const TString& data) = 0;

        virtual TString Load() = 0;
    };

    class IStateDirectoryLock {
    public:
        virtual ~IStateDirectoryLock() = default;

        virtual void Acquire() = 0;

        virtual void Release() = 0;

        virtual bool WasGracefulShutdown() = 0;

        static THolder<IStateDirectoryLock> Make(const TFsPath& directory, TFlowPluginContext& ctx);
    };

    class TFileProtoStorage {
    public:
        TFileProtoStorage(const TFsPath& directory, TScopeLogger& logger);

        bool TryLoad(google::protobuf::Message& target) const;

        void Save(const google::protobuf::Message& source);

    private:
        static constexpr size_t CrcSize = sizeof(ui32);
        static constexpr size_t SizeSize = sizeof(size_t);
        static constexpr size_t HeaderSize = CrcSize + SizeSize;

    private:
        const TFsPath FilePath;
        THolder<TFileAccessor> File;
        TScopeLogger Logger;
    };
}
