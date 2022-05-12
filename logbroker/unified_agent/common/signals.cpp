#include "signals.h"

#include <logbroker/unified_agent/common/common.h>
#include <logbroker/unified_agent/common/util/clock.h>

#include <util/string/builder.h>
#include <util/system/env.h>
#include <util/string/printf.h>
#include <util/system/compiler.h>
#include <util/system/getpid.h>
#include <util/system/thread.h>

#include <atomic>
#include <cerrno>
#include <csignal>

#ifdef _win_
#include "synchapi.h"
#endif

namespace {
    using namespace NUnifiedAgent;

    TManualEvent ShutdownEvent;
    std::atomic<TThread::TId> FatalErrorThreadId{0};
    std::atomic<int> ShutdownSignal{0};
    TMutex LogMutex{};

     enum class ELogType {
        Error,
        Info
    };

    void Log(ELogType logType, const TStringBuf message) {
        TStringBuilder b;
        b << FormatIsoLocal(TClock::Now())
             << " " << GetPID()
             << " " << TThread::CurrentThreadId()
             << " " << (logType == ELogType::Error ? "!ERROR" : "info")
             << " " << message << Endl;
        auto guard = Guard(LogMutex);
        try {
            Cerr.Write(b.data(), b.size());
        } catch (...) {
        }
    }

    TString FormatWithBacktrace(const TString& message) {
        TStringStream s;
        s << message << ":\n";
        FormatBackTrace(&s);
        s.Flush();
        return s.Str();
    }

    void SetSignal(int signum, void (*handler)(int)) {
        std::signal(signum, handler);
    }

    void ResetSignal(int signum) {
        SetSignal(signum, SIG_DFL);
    }

    void StartShutdown(int signum) {
        auto empty = 0;
        if (ShutdownSignal.compare_exchange_strong(empty, signum)) {
            ShutdownEvent.Signal();
        }
    }

    void ReRaiseSignal(int signal) {
        std::signal(signal, SIG_DFL);
        if (signal != SIGABRT) {
            std::raise(signal);
        }
    }

    void FatalSignalHandler(int signal) {
        TThread::TId threadId = 0;
        const auto currentThreadId = TThread::CurrentThreadId();
        if (!FatalErrorThreadId.compare_exchange_strong(threadId, currentThreadId)) {
            if (threadId == currentThreadId) {
                Log(ELogType::Error, FormatWithBacktrace("fatal error recursion"));
                ReRaiseSignal(signal);
            } else {
                Sleep(TDuration::Days(1));
            }
        }
        const auto message = FormatWithBacktrace(Sprintf("fatal error, signal [%d]", signal));
        Log(ELogType::Error, message);
        ReRaiseSignal(signal);
    }
}

namespace NUnifiedAgent {
    void SetShutdownSignalHandler() {
        SetSignal(SIGINT, StartShutdown);
        SetSignal(SIGTERM, StartShutdown);
    }

    void ResetShutdownSignalHandler() {
        ShutdownSignal.store(-1);
        ResetSignal(SIGINT);
        ResetSignal(SIGTERM);
    }

    void SetFatalSignalHandler() {
        SetSignal(SIGSEGV, FatalSignalHandler);
        SetSignal(SIGFPE, FatalSignalHandler);
        SetSignal(SIGILL, FatalSignalHandler);
        SetSignal(SIGABRT, FatalSignalHandler);
    }

#ifdef _win_
    bool WaitForNamedEvent(const TString& name, const TDuration& timeout, bool silent = false) {
        const auto eventHandle = CreateEventA(nullptr, false, false, name.Data());
        if (!eventHandle) {
            Log(ELogType::Error, "Failed to get event, shutdown");
            return true;
        }
        auto dwWaitResult = WaitForSingleObject(eventHandle, timeout.MilliSeconds());
        switch (dwWaitResult) {
            case WAIT_OBJECT_0:
                if (!silent) {
                    Log(ELogType::Info, "shutdown started, reason: named event received");
                }
                break;
            case WAIT_TIMEOUT:
                return false;
            default:
                Log(ELogType::Error, Sprintf("shutdown started, reason: wait error (%d)", GetLastError()));
                break;
        }
        return true;
    }
#endif

    bool WaitForShutdown(const TDuration& timeout, bool silent) {
#ifdef _win_
        if (const auto eventName = GetEnv("SHUTDOWN_EVENT_NAME"); !eventName.Empty()) {
            return WaitForNamedEvent(eventName, timeout, silent);
        }
#endif

        // под tsan почему-то сигнал продалбывается иногда
#ifdef _tsan_enabled_
        const auto waitStartTs = ::Now();
        while (ShutdownSignal.load() == 0) {
            ShutdownEvent.WaitT(TDuration::MilliSeconds(100));
            if ((::Now() - waitStartTs) > timeout) {
                return false;
            }
        }
#else
        if (!ShutdownEvent.WaitT(timeout)) {
            return false;
        }
#endif

        if (!silent) {
            const auto signal = ShutdownSignal.load();
            const TString shutdownReason = signal > 0
                ? Sprintf("signal [%d] received", signal)
                : TString("work completed");
            Log(ELogType::Info, Sprintf("shutdown started, reason: %s", shutdownReason.c_str()));
        }
        return true;
    }

    TManualEvent& GetShutdownEvent() {
        return ShutdownEvent;
    }

    void TriggerShutdown() {
        StartShutdown(-1);
    }
}
