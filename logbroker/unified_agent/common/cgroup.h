#pragma once

#include <util/generic/noncopyable.h>
#include <util/generic/string.h>
#include <util/folder/path.h>

namespace NUnifiedAgent {
    //shamelessly stolen from yt/yt/ytlib/cgroup/cgroup.h

    class TCGroup: private TNonCopyable {
    public:
        explicit TCGroup(const TString& fullPath);

        TCGroup(const TString& type, const TString& name);

        void AddTask(int pid) const;

        TVector<int> GetTasks() const;

        inline bool Exists() const noexcept {
            return FullPath.Exists();
        }

        inline const TString& GetFullPath() const noexcept {
            return FullPath;
        }

        void EnsureExists() const;

        void Remove() const;

        void Kill() const;

    protected:
        void Append(const TString& name, const TString& value) const;

        void Set(const TString& name, const TString& value) const;

        TVector<TString> ReadAllValues(const TString& fileName) const;

        inline TString GetPath(const TString& filename) const noexcept {
            return FullPath / filename;
        }

    private:
        TFsPath FullPath;
    };

    class TMemoryCgroup: public TCGroup {
    public:
        static const TString Name;

        explicit TMemoryCgroup(const TString& name);

        void SetLimitInBytes(i64 bytes) const;
    };
}
