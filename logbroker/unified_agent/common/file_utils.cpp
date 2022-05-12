#include "file_utils.h"

#include <logbroker/unified_agent/common/util/clock.h>

#include <util/system/env.h>
#include <util/system/fstat.h>
#include <util/string/printf.h>
#include <util/string/subst.h>

#include <cerrno>

#ifdef _win_
#include <fileapi.h>
#endif

namespace NUnifiedAgent  {
    TString TFileUtils::MakeUnique(const TString& fileName) {
        auto result = fileName;
        size_t index = 2;
        while (true) {
            if (!TFsPath(result).Exists()) {
                return result;
            }
            result = fileName + "." + ToString(index);
            ++index;
        }
    }

    TVector<TString> TFileUtils::Children(const TFsPath& directory, bool excludeHidden) {
        TVector<TString> children;
        directory.ListNames(children);
        if (!excludeHidden) {
            return children;
        }
        TVector<TString> childrenWithoutHidden;
        childrenWithoutHidden.reserve(children.size());
        for (const auto& s: children) {
            if (!s.StartsWith(".")) {
                childrenWithoutHidden.push_back(s);
            }
        }
        return childrenWithoutHidden;
    }

    void TFileUtils::FlushDirectory(const TString& path) {
#if defined(_unix_)
        TFileAccessor(path, RdOnly).Flush();
#elif defined(_win_)
        FHANDLE h = CreateFile(path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
        TFile(h).Flush();
#else
        Y_FAIL("unsupported platform");
#endif
    }

    size_t TFileUtils::GetFileSize(const TString& filePath) {
        const auto result = GetFileLength(filePath);
        Y_VERIFY(result >= 0, "invalid length [%ld], file [%s]", result, filePath.c_str());
        return static_cast<ui64>(result);
    }

    auto TFileUtils::UniqueChildWithTimestamp(const TFsPath& directoryPath, const TString& suffix)
        -> TNameWithTimestamp
    {
        auto timestamp = TClock::Now();
        while (true) {
            TString name;
            {
                TStringOutput output(name);
                output << timestamp;
                SubstGlobal(name, ':', '-');
                SubstGlobal(name, '.', '-');
                output << suffix;
            }
            const auto filePath = directoryPath / name;
            if (!filePath.Exists()) {
                return {name, timestamp};
            }
            timestamp += TDuration::MicroSeconds(1);
        }
    }

    TFileAccessor::TFileAccessor(const TString& filePath, EOpenMode mode)
        : TFileAccessor(filePath, Open(filePath, mode))
    {
    }

    TFileAccessor::TFileAccessor(const TString& filePath, TFile&& file)
        : FilePath_(filePath)
        , File_(std::move(file))
        , NoFsync_(!GetEnv("UA_NO_FSYNC").empty())
    {
    }

    size_t TFileAccessor::GetSize() const {
        const auto result = File_.GetLength();
        Y_VERIFY(result >= 0, "invalid length [%ld], file [%s]", result, File_.GetName().c_str());
        return static_cast<ui64>(result);
    }

    void TFileAccessor::Resize(size_t size) {
        try {
            File_.Resize(size);
        } catch(...) {
            Y_FAIL("resize error, file [%s], error %s",
                   FilePath_.c_str(), CurrentExceptionMessage().c_str());
        }
    }

    void TFileAccessor::Flush() {
        if (NoFsync_) {
            return;
        }
        try {
            File_.Flush();
        } catch(...) {
            Y_FAIL("flush error, file [%s], error %s",
                   FilePath_.c_str(), CurrentExceptionMessage().c_str());
        }
    }

    void TFileAccessor::Write(const void* buf, size_t len) {
        try {
            File_.Write(buf, len);
        } catch(...) {
            Y_FAIL("write error, file [%s], error %s",
                   FilePath_.c_str(), CurrentExceptionMessage().c_str());
        }
    }

    size_t TFileAccessor::Pread(void* buf, size_t len, i64 offset) {
        try {
            return File_.Pread(buf, len, offset);
        } catch(...) {
            Y_FAIL("pread error, file [%s], error %s",
                   FilePath_.c_str(), CurrentExceptionMessage().c_str());
        }
    }

    void TFileAccessor::Pwrite(const void* buf, size_t len, i64 offset) {
        try {
            return File_.Pwrite(buf, len, offset);
        } catch(...) {
            Y_FAIL("pwrite error, file [%s], error %s",
                FilePath_.c_str(), CurrentExceptionMessage().c_str());
        }
    }

    TFMaybe<TFileAccessor> TFileAccessor::TryOpenForReading(const TString& filePath, bool sequential) {
        try {
            return MakeFMaybe(TFileAccessor(
                filePath,
                TFile(filePath, OpenExisting | RdOnly | (sequential ? Seq : static_cast<EOpenModeFlag>(0)))));
        } catch (const TFileError& e) {
#ifdef _win_
            if (e.Status() == ERROR_FILE_NOT_FOUND || e.Status() == ERROR_PATH_NOT_FOUND) {
#else
            if (e.Status() == ENOENT) {
#endif
                return Nothing();
            }
            Y_FAIL("open error, file [%s], errno [%d]", filePath.c_str(), e.Status());
        } catch (...) {
            Y_FAIL("open error, file [%s]", filePath.c_str());
        }
    }

    TFile TFileAccessor::Open(const TString& filePath, EOpenMode mode) {
        try {
            return TFile(filePath, mode);
        } catch (...) {
            Y_FAIL("open error, file [%s], error %s",
                   filePath.c_str(), CurrentExceptionMessage().c_str());
        }
    }

    TFileReader::TFileReader(const TString& filePath, size_t cacheSize)
        : TFileReader(TFileAccessor(filePath, OpenExisting | RdOnly | Seq), cacheSize)
    {
    }

    TFileReader::TFileReader(TFileAccessor&& file, size_t cacheSize)
        : File(std::move(file))
        , CacheSize(cacheSize)
        , Buffer()
        , BufferOffset(0)
        , Offset(0)
        , ReadOffset(0)
    {
    }

    void TFileReader::Reset(ui64 offset) {
        Buffer.Clear();
        BufferOffset = 0;
        Offset = offset;
        ReadOffset = offset;
    }

    bool TFileReader::Read(size_t size, void* target, bool* exhausted) {
        const auto* source = Read(size, exhausted);
        if (source) {
            memcpy(target, source, size);
            return true;
        }
        return false;
    }

    const char* TFileReader::Read(size_t size, bool* exhausted) {
        if (Buffer.Size() - BufferOffset < size) {
            Buffer.ChopHead(BufferOffset);
            BufferOffset = 0;
            auto bytesToRead = size - Buffer.Size();
            if (Buffer.Size() < CacheSize) {
                bytesToRead = Max(CacheSize - Buffer.Size(), bytesToRead);
            }
            Buffer.Reserve(Buffer.Size() + bytesToRead);
            const auto bytesRead = File.Pread(Buffer.Pos(), bytesToRead, ReadOffset);
            ReadOffset += bytesRead;
            Buffer.Resize(Buffer.Size() + bytesRead);
            if (Buffer.Size() < size) {
                if (exhausted) {
                    *exhausted = Buffer.Size() == 0;
                }
                return nullptr;
            }
        }
        const auto* result = Buffer.Data() + BufferOffset;
        BufferOffset += size;
        Offset += size;
        if (exhausted) {
            *exhausted = false;
        }
        return result;
    }
}
