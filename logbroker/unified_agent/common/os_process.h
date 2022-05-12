#pragma once

#include <logbroker/unified_agent/common/util/f_maybe.h>

#include <util/generic/hash.h>
#include <util/generic/vector.h>
#include <util/system/spinlock.h>
#include <util/datetime/base.h>

namespace NUnifiedAgent {
    class IOSProcess {
    public:
        IOSProcess(const TString& cmd) Y_NO_SANITIZE("thread") Y_NO_SANITIZE("memory") Y_NO_SANITIZE("address")
            : Cmd(cmd)
            , Pid(0)
            , ExitStatus(Nothing()) {
        }

        virtual ~IOSProcess() = default;

        void Wait(TDuration timeout = TDuration::Seconds(10));

        virtual void KillAndWait() = 0;

        void TermAndWait();

        virtual void Term() = 0;

        inline pid_t GetPid() const noexcept {
            return Pid;
        }

        inline int GetExitStatus() const noexcept {
            return *ExitStatus;
        }

        inline TFMaybe<int> GetExitSignal() const noexcept {
            return ExitSignal;
        }

        inline bool Running() const noexcept {
            return !ExitStatus.Defined();
        }

    protected:
        virtual bool TrySetExitStatus() = 0;

    protected:
        TString Cmd;
        pid_t Pid;
        TFMaybe<int> ExitStatus;
        TFMaybe<int> ExitSignal;
    };

    struct TShellCommandResult final: public TAtomicRefCount<TShellCommandResult> {
        TString CmdDesc{};

        int ExitCode{0};
        TString StdOut{};
        TString StdErr{};

        TString Describe() const;

        TString CheckOk() const;

        static void SetLast(const TIntrusivePtr<TShellCommandResult>& command);

        static TIntrusivePtr<TShellCommandResult> GetLast();

        static void ResetLast();

    private:
        static TIntrusivePtr<TShellCommandResult> Last;
        static TAdaptiveLock Lock;
    };

    TIntrusivePtr<TShellCommandResult> ExecuteCommand(const TString& cmd,
        const TVector<TString>& args,
        const THashMap<TString, TString>& env = {},
        const TDuration& timeout = TDuration::Seconds(10));

    THolder<IOSProcess> MakeOSProcess(const TString& cmd,
        const TVector<TString>& args, const THashMap<TString, TString>& env,
        const TString& stdoutFilePath, const TString& stderrFilePath);

    void EnsureSubreaperInitialized();
}
