#pragma once

#include <util/datetime/base.h>
#include <util/system/getpid.h>
#include <util/system/types.h>

namespace NUnifiedAgent {
    struct TProcStat {
        ui64 Rss;
        ui64 VolCtxSwtch;
        ui64 NonvolCtxSwtch;

        int Pid;
        char Comm[256];
        char State;
        int Ppid;
        int Pgrp;
        int Session;
        int TtyNr;
        int TPgid;
        unsigned Flags;
        unsigned long MinFlt;
        unsigned long CMinFlt;
        unsigned long MajFlt;
        unsigned long CMajFlt;
        unsigned long Utime;
        unsigned long Stime;
        long CUtime;
        long CStime;
        long Priority;
        long Nice;
        long NumThreads;
        long ItRealValue;
        unsigned long long StartTime;
        unsigned long Vsize;
        long RssPages;
        unsigned long RssLim;
        ui64 FileRss;
        ui64 AnonRss;

        TProcStat();

        bool Fill();

        static bool CheckIfProcessExists(TProcessId pid);

        static TDuration GetUTime(TProcessId pid);

#ifdef _linux_
    private:
        static void FillFromProcFs(TProcStat& dst, TProcessId pid);
#endif

    private:
        long PageSize = 0;
    };

    class TCpuLoadMeter {
    public:
        TCpuLoadMeter(const TProcessId pid);

        double Measure() const;

    private:
        const TProcessId Pid;
        const TDuration StartUtime;
        const TInstant StartWallClock;
    };
}
