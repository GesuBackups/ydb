#include "cgroup.h"

#include <logbroker/unified_agent/common/system.h>

#include <util/generic/hash.h>
#include <util/stream/file.h>
#include <util/string/split.h>
#include <util/system/yield.h>

namespace NUnifiedAgent {
    namespace {
        const auto CGroupRootPath = TFsPath("/sys/fs/cgroup");

        THashMap<TString, TString> ParseProcessCGroups(const TString& str) {
            THashMap<TString, TString> result;

            TVector<TString> values;
            StringSplitter(str.data()).SplitBySet(":\n").SkipEmpty().Collect(&values);
            for (size_t i = 0; i + 2 < values.size(); i += 3) {
                // Check format.
                FromString<int>(values[i]);

                const auto& subsystemsSet = values[i + 1];
                const auto& name = values[i + 2];

                TVector<TString> subsystems;
                StringSplitter(subsystemsSet.data()).Split(',').SkipEmpty().Collect(&subsystems);
                for (const auto& subsystem : subsystems) {
                    if (!subsystem.StartsWith("name=")) {
                        int start = 0;
                        if (name.StartsWith("/")) {
                            start = 1;
                        }
                        result[subsystem] = name.substr(start);
                    }
                }
            }

            return result;
        }

        TString GetParentFor(const TString& type) {
            const auto rawData = TUnbufferedFileInput("/proc/self/cgroup").ReadAll();
            const auto result = ParseProcessCGroups(rawData);
            auto it = result.find(type);
            Y_VERIFY(it != result.end());
            return it->second;
        }
    }

    TCGroup::TCGroup(const TString& fullPath)
        : FullPath(fullPath)
    {
    }

    TCGroup::TCGroup(const TString& type, const TString& name)
        : FullPath(CGroupRootPath / type / GetParentFor(type) / name)
    {
    }

    void TCGroup::Append(const TString& name, const TString& value) const {
        const auto path = GetPath(name);
        TUnbufferedFileOutput output(TFile(path, EOpenModeFlag::ForAppend));
        output << value;
    }

    void TCGroup::Set(const TString& name, const TString& value) const {
        const auto path = GetPath(name);
        TUnbufferedFileOutput output(TFile(path, EOpenModeFlag::WrOnly));
        output << value;
    }

    void TCGroup::AddTask(int pid) const {
        Append("tasks", ToString(pid));
    }

    TVector<TString> TCGroup::ReadAllValues(const TString& fileName) const {
        const auto raw = TUnbufferedFileInput(fileName).ReadAll();

        TVector<TString> values;
        StringSplitter(raw.data())
            .SplitBySet(" \n")
            .SkipEmpty()
            .Collect(&values);
        return values;
    }

    TVector<int> TCGroup::GetTasks() const {
        auto values = ReadAllValues(GetPath("tasks"));
        auto results = TVector<int>(Reserve(values.size()));
        for (const auto& value : values) {
            const auto pid = FromString<int>(value);
            results.push_back(pid);
        }
        return results;
    }

    void TCGroup::EnsureExists() const {
        FullPath.MkDirs(0755);
    }

    void TCGroup::Remove() const {
        FullPath.DeleteIfExists();
    }

    void TCGroup::Kill() const {
        if (!FullPath.Exists()) {
            return;
        }

        while (true) {
            auto pids = GetTasks();
            if (pids.empty())
                break;

            for (const auto pid: pids) {
                DoKill(pid, SIGKILL);
            }

            ThreadYield();
        }
    }

    const TString TMemoryCgroup::Name = "memory";

    TMemoryCgroup::TMemoryCgroup(const TString& name)
        : TCGroup(Name, name)
    {
    }

    void TMemoryCgroup::SetLimitInBytes(i64 bytes) const {
        Set("memory.limit_in_bytes", ToString(bytes));
    }
}
