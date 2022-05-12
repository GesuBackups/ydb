#include "proc_stat.h"

#ifdef _linux_
#include <util/generic/string.h>
#include <util/stream/file.h>
#include <util/string/cast.h>
#include <util/string/split.h>

#include <cstdlib>
#else
#include <util/system/rusage.h>
#include <util/system/mem_info.h>
#endif

#ifdef _win_
#include <tlhelp32.h>
#include <psapi.h>
#endif

#ifdef _darwin_
#include <libproc.h>
#endif

namespace NUnifiedAgent {
    namespace {
#ifdef _linux_
        template <typename TVal>
        static bool ExtractVal(const TString& str, const TString& name, TVal& res) {
            if (!str.StartsWith(name))
                return false;
            size_t pos = name.size();
            while (pos < str.size() && (str[pos] == ' ' || str[pos] == '\t')) {
                pos++;
            }
            res = std::atol(str.data() + pos);
            return true;
        }

        float TicksPerMillisec() {
            return sysconf(_SC_CLK_TCK) / 1000.0;
        }

        long ObtainPageSize() {
            return sysconf(_SC_PAGESIZE);
        }
#elif defined(_win_)
        long GetNumberOfThreads(TProcessId pid) {
            long result = 0;
            HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (h != INVALID_HANDLE_VALUE) {
                THREADENTRY32 te;
                te.dwSize = sizeof(te);
                if (Thread32First(h, &te)) {
                    do {
                        if (te.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) +
                                             sizeof(te.th32OwnerProcessID) &&
                            te.th32OwnerProcessID == pid) {
                            result += 1;
                        }
                        te.dwSize = sizeof(te);
                    } while (Thread32Next(h, &te));
                }
                CloseHandle(h);
            }
            return result;
        }

        TDuration FiletimeToDuration(const FILETIME& ft) {
            union {
                ui64 ft_scalar;
                FILETIME ft_struct;
            } nt_time;
            nt_time.ft_struct = ft;
            return TDuration::MicroSeconds(nt_time.ft_scalar / 10);
        }
#elif defined(_darwin_)
        long GetNumberOfThreads(TProcessId pid) {
            // copy-paste from https://a.yandex-team.ru/arc/trunk/arcadia/contrib/python/psutil/psutil/arch/osx/process_info.c?rev=r8799933
            struct proc_taskinfo pti;
            int ret = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &pti, PROC_PIDTASKINFO_SIZE);
            if ((ret <= 0) || ((unsigned long)ret < sizeof(pti))) {
                ythrow TSystemError() << "proc_pidinfo() failed";
            }
            return pti.pti_threadnum;
        }
#else
        long GetNumberOfThreads(TProcessId) {
            Y_FAIL("unsupported platform");
        }
#endif
    }

    TProcStat::TProcStat() {
        PageSize = 0;
        Zero(*this);
    }

#ifdef _linux_
    void TProcStat::FillFromProcFs(TProcStat& dst, TProcessId pid) {
        TFileInput proc("//proc//" + ToString(pid) + "//status");
        TString str;
        while (proc.ReadLine(str)) {
            if (ExtractVal(str, "VmRSS:", dst.Rss))
                continue;
            if (ExtractVal(str, "voluntary_ctxt_switches:", dst.VolCtxSwtch))
                continue;
            if (ExtractVal(str, "nonvoluntary_ctxt_switches:", dst.NonvolCtxSwtch))
                continue;
        }
        // Convert from kB to bytes
        dst.Rss *= 1024;

        float tickPerMillisec = TicksPerMillisec();

        TFileInput procStat("//proc//" + ToString(pid) + "//stat");
        procStat.ReadLine(str);
        if (!str.empty()) {
            sscanf(str.data(),
                "%d %s %c %d %d %d %d %d %u %lu %lu "
                "%lu %lu %lu %lu %ld %ld %ld %ld %ld "
                "%ld %llu %lu %ld %lu",
                &dst.Pid, dst.Comm, &dst.State, &dst.Ppid, &dst.Pgrp, &dst.Session, &dst.TtyNr, &dst.TPgid, &dst.Flags, &dst.MinFlt, &dst.CMinFlt,
                &dst.MajFlt, &dst.CMajFlt, &dst.Utime, &dst.Stime, &dst.CUtime, &dst.CStime, &dst.Priority, &dst.Nice, &dst.NumThreads,
                &dst.ItRealValue, &dst.StartTime, &dst.Vsize, &dst.RssPages, &dst.RssLim);
            dst.Utime /= tickPerMillisec;
            dst.Stime /= tickPerMillisec;
            dst.CUtime /= tickPerMillisec;
            dst.CStime /= tickPerMillisec;
        }

        TFileInput statm("//proc//" + ToString(pid) + "//statm");
        statm.ReadLine(str);
        TVector<TString> fields;
        StringSplitter(str).Split(' ').SkipEmpty().Collect(&fields);
        if (fields.size() >= 7) {
            ui64 resident = FromString<ui64>(fields[1]);
            ui64 shared = FromString<ui64>(fields[2]);
            if (dst.PageSize == 0) {
                dst.PageSize = ObtainPageSize();
            }
            dst.FileRss = shared * dst.PageSize;
            dst.AnonRss = (resident - shared) * dst.PageSize;
        }
    }
#endif

    bool TProcStat::Fill() {
        auto pid = GetPID();
        try {
#if defined(_linux_)
            FillFromProcFs(*this, pid);
#else
            const auto rusage = TRusage::Get();
            Utime = rusage.Utime.MilliSeconds();;
            Stime = rusage.Stime.MilliSeconds();;
            MajFlt = rusage.MajorPageFaults;

            const auto memInfo = NMemInfo::GetMemInfo(pid);
            Rss = memInfo.RSS;
            Vsize = memInfo.VMS;
            Pid = pid;
            MinFlt = 0;
            VolCtxSwtch = 0;
            NonvolCtxSwtch = 0;
            NumThreads = GetNumberOfThreads(pid);
#endif
        } catch (...) {
            Cerr << CurrentExceptionMessage() << Endl;
            return false;
        }
        return true;
    }

    bool TProcStat::CheckIfProcessExists(TProcessId pid) {
#if defined(_unix_)
        return kill(pid, 0) == 0;
#elif defined(_win_)
        return GetNumberOfThreads(pid) > 0;
#else
        Y_UNUSED(pid);
        Y_FAIL("unsupported platform");
#endif
    }

    TDuration TProcStat::GetUTime(TProcessId pid) {
#if defined(_linux_)
        TProcStat stat;
        TProcStat::FillFromProcFs(stat, pid);
        return TDuration::MilliSeconds(stat.Utime);
#elif defined(_win_)
        FILETIME starttime;
        FILETIME exittime;
        FILETIME kerneltime;
        FILETIME usertime;
        auto hProcess = OpenProcess( PROCESS_ALL_ACCESS, false, pid );
        Y_VERIFY(hProcess, "failed to get process handle");
        if (GetProcessTimes(hProcess, &starttime, &exittime, &kerneltime, &usertime) == 0) {
            ythrow TSystemError() << "GetProcessTimes failed";
        }
        return FiletimeToDuration(usertime);
#else
        struct proc_taskinfo pti;
        int ret = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &pti, PROC_PIDTASKINFO_SIZE);
        if ((ret <= 0) || ((unsigned long)ret < sizeof(pti))) {
            ythrow TSystemError() << "proc_pidinfo() failed";
        }
        return TDuration::Seconds(pti.pti_total_user / 1000000000.0);
#endif
    }

    TCpuLoadMeter::TCpuLoadMeter(const TProcessId pid)
        : Pid(pid)
        , StartUtime(TProcStat::GetUTime(Pid))
        , StartWallClock(Now()) {}

    double TCpuLoadMeter::Measure() const {
        const auto endUserTime = TProcStat::GetUTime(Pid);
        const auto endTime = ::Now();

        const auto durationMs = (endTime - StartWallClock).MilliSeconds();
        const auto diffCpuUserTimeMs = (endUserTime - StartUtime).MilliSeconds();
        return static_cast<double>(diffCpuUserTimeMs) / durationMs * 100.0;
    }
}
