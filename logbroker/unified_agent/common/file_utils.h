#pragma once

#include <logbroker/unified_agent/common/util/f_maybe.h>

#include <util/datetime/base.h>
#include <util/folder/path.h>
#include <util/generic/buffer.h>
#include <util/generic/string.h>
#include <util/system/file.h>

namespace NUnifiedAgent {
    class TFileUtils {
    public:
        static TString MakeUnique(const TString& fileName);

        static TVector<TString> Children(const TFsPath& directory, bool excludeHidden = false);

        static void FlushDirectory(const TString& path);

        static size_t GetFileSize(const TString& filePath);

        struct TNameWithTimestamp {
            TString Name;
            TInstant Timestamp;
        };

        static TNameWithTimestamp UniqueChildWithTimestamp(const TFsPath& directoryPath, const TString& suffix = "");
    };

    class TFileAccessor {
    public:
        TFileAccessor(const TString& filePath, EOpenMode mode);

        size_t GetSize() const;

        inline const TFsPath& FilePath() const noexcept {
            return FilePath_;
        }

        inline TFile& GetFile() noexcept {
            return File_;
        }

        void Resize(size_t size);

        void Flush();

        void Write(const void* buf, size_t len);

        size_t Pread(void* buf, size_t len, i64 offset);

        void Pwrite(const void* buf, size_t len, i64 offset);

        static TFMaybe<TFileAccessor> TryOpenForReading(const TString& filePath, bool sequential);

    private:
        static TFile Open(const TString& filePath, EOpenMode mode);

        explicit TFileAccessor(const TString& filePath, TFile&& file);

    private:
        TFsPath FilePath_;
        TFile File_;
        bool NoFsync_;
    };

    class TFileReader {
    public:
        TFileReader(const TString& filePath, size_t cacheSize);

        TFileReader(TFileAccessor&& file, size_t cacheSize);

        const TString& FilePath() const {
            return File.FilePath();
        }

        size_t GetSize() const {
            return File.GetSize();
        }

        ui64 GetOffset() const {
            return Offset;
        }

        void Reset(ui64 offset);

        bool Read(size_t size, void* target, bool* exhausted = nullptr);

        const char* Read(size_t size, bool* exhausted = nullptr);

        template <typename T>
        bool Read(T& target, bool* exhausted = nullptr) {
            return Read(sizeof(T), &target, exhausted);
        }

    private:
        TFileAccessor File;
        size_t CacheSize;
        TBuffer Buffer;
        ui64 BufferOffset;
        ui64 Offset;
        ui64 ReadOffset;
    };
}
