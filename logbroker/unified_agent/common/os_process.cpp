#include "os_process.h"

#include <logbroker/unified_agent/common/common.h>

#include <util/string/printf.h>
#include <util/string/subst.h>
#include <util/system/file.h>
#include <util/system/tempfile.h>
#include <util/stream/file.h>

#ifdef _win_
#include <util/generic/list.h>
#include <util/generic/string.h>
#include <util/generic/guid.h>
#include <util/system/shellcommand.h>

#include <mutex>
#else
#include <logbroker/unified_agent/common/system.h>

#include <util/string/join.h>

#include <cerrno>
#include <sys/types.h>
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace NUnifiedAgent {
    void IOSProcess::Wait(TDuration timeout) {
        Y_ENSURE(!ExitStatus.Defined(), Sprintf("exit status is already set to [%d]", *ExitStatus));

        const auto deadline = Now() + timeout;
        Cout << Now() << ' '
             << "start waiting for [" << Cmd << "] with pid [" << Pid << "] to exit, "
             << "deadline [" << deadline << "]" << Endl;
        while (true) {
            const auto now = Now();
            if (now >= deadline) {
                const auto message = Sprintf("timeout waiting for [%s] with pid [%u] to exit, backtrace:\n%s",
                    Cmd.c_str(), Pid, CaptureBacktrace().c_str());
                Cout << now << ' ' << message << Endl;
                ythrow yexception() << message;
            }
            if (TrySetExitStatus()) {
                Cout << Now() << " [" << Cmd << "] with pid [" << Pid << "] exited" << Endl;
                break;
            }
            Cout << Now() << " waiting for [" << Cmd << "] with pid [" << Pid << "] to exit" << Endl;
            Sleep(TDuration::MilliSeconds(10));
        }
    }

    void IOSProcess::TermAndWait() {
        Term();
        Wait();
    }

#if defined(_unix_)
    class TOSProcess: public IOSProcess {
    public:
        TOSProcess(const TString& cmd,
            const TVector<TString>& args, const THashMap<TString, TString>& env,
            const TString& stdoutFilePath, const TString& stderrFilePath)
            : IOSProcess(cmd)
        {
            TVector<char*> execArgs;
            {
                execArgs.reserve(args.size() + 2);
                execArgs.push_back(const_cast<char*>(cmd.data()));
                for (const auto& i : args) {
                    execArgs.push_back(const_cast<char*>(i.data()));
                }
                execArgs.push_back(nullptr);
            }

            TVector<char*> execEnv;
            auto mergedEnv = MergeCurrentEnv(THashMap<TString, TString>(env));
            {
                for (auto& [name, value]: mergedEnv) {
                    value = Sprintf("%s=%s", name.c_str(), value.c_str());
                }

                execEnv.reserve(mergedEnv.size() + 1);
                for (const auto& [name, value]: mergedEnv) {
                    execEnv.push_back(const_cast<char*>(value.data()));
                }
                execEnv.push_back(nullptr);
            }

            TFileHandle stdoutFile(stdoutFilePath, OpenAlways | WrOnly | Seq | ::ForAppend);
            TFileHandle stderrFile(stderrFilePath, OpenAlways | WrOnly | Seq | ::ForAppend);

            // Объезжаем баг tsan https://github.com/google/sanitizers/issues/1116
            // "It was assumed that fork does not call any instrumented user code".
            // Если были pthread_at_fork, получаем рейсы/подвисания.
            // vfork не помогает, т.к. tsan реврайтит его в обычный fork зачем-то.
            // Простой syscall tsan вроде не перехватывает, и колбаки pthread_at_fork не зовутся.
            // Коммит https://github.com/llvm/llvm-project/commit/be41a98ac222f33ed5558d86e1cede67249e99b5
            // должен полечить проблему, можно будет выпилить эти костыли. В llvm 10.0.0 (24.03.2020) его еще нет.

            static void* vforkPtr = dlsym(RTLD_DEFAULT, "vfork");
            Y_VERIFY(vforkPtr, "vfork not found");

            Pid = reinterpret_cast<pid_t(*)()>(vforkPtr)();
            if (Pid == 0) {
                if (stdoutFile.Duplicate2Posix(STDOUT_FILENO) != STDOUT_FILENO) {
                    _exit(41);
                }
                if (stderrFile.Duplicate2Posix(STDERR_FILENO) != STDERR_FILENO) {
                    _exit(42);
                }

                sigset_t mask;
                sigemptyset(&mask);
                sigprocmask(0, nullptr, &mask);
                sigprocmask(SIG_UNBLOCK, &mask, nullptr);

                ::execve(execArgs[0], execArgs.data(), execEnv.data());

                _exit(43);
            }
            Y_VERIFY(Pid != -1, "SYS_clone failed, errno [%d]", errno);
        }

        ~TOSProcess() override {
            try {
                if (Running()) {
                    KillAndWait();
                }
            } catch (...) {
                Y_FAIL("unexpected exception: %s", CurrentExceptionMessage().c_str());
            }
        }

        void KillAndWait() final {
            DoKill(Pid, SIGKILL);
            Wait();
        }

        void Term() override {
            DoKill(Pid, SIGTERM);
        }

    private:
        bool TrySetExitStatus() override {
            int status;
            const pid_t waitPidResult = DoWaitPid(Pid, &status, WNOHANG);
            if (waitPidResult != 0) {
                Y_VERIFY(waitPidResult == Pid, "unexpected waitpid result");
                ExitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status);
                if (WIFSIGNALED(status)) {
                    ExitSignal = WTERMSIG(status);
                }
                return true;
            }
            return false;
        }
    };
#elif defined(_win_)
    class TOSProcess: public IOSProcess {
    public:
        TOSProcess(const TString& cmd,
            const TVector<TString>& args, const THashMap<TString, TString>& env,
            const TString& stdoutFilePath, const TString& stderrFilePath)
            : IOSProcess(cmd)
            , StdoutFile(TFile::ForAppend(stdoutFilePath))
            , StderrFile(TFile::ForAppend(stderrFilePath))
            , StdoutStream(StdoutFile)
            , StderrStream(StderrFile)
            , StopEventName("STOP_EVENT_" + CreateGuidAsString())
            , StopEventHandle([&]() {
                const auto event = CreateEventExA(nullptr, StopEventName.Data(), 0, EVENT_ALL_ACCESS);
                Y_VERIFY(event, "failed to create stop event for new process");
                return event;
            }())
            , ShellCommand(cmd, {args.begin(), args.end()}, [&]() {
                auto result = TShellCommandOptions()
                    .SetClearSignalMask(true)
                    .SetUseShell(false)
                    .SetAsync(true)
                    .SetOutputStream(&StdoutStream)
                    .SetErrorStream(&StderrStream);
                result.Environment = MergeCurrentEnv(THashMap<TString, TString>(env));
                result.Environment["SHUTDOWN_EVENT_NAME"] = StopEventName;
                return result;
            }())
        {
            ShellCommand.Run();
            Pid = ShellCommand.GetPid();
        }

        ~TOSProcess() override {
            try {
                if (Running()) {
                    KillAndWait();
                }
            } catch (...) {
                Y_FAIL("unexpected exception: %s", CurrentExceptionMessage().c_str());
            }
        }

        void KillAndWait() final {
            ShellCommand.Terminate();
            ShellCommand.Wait();
        }

        void Term() override {
            Y_VERIFY(SetEvent(StopEventHandle));
        }

    private:
        bool TrySetExitStatus() override {
            if (ShellCommand.GetStatus() == TShellCommand::SHELL_RUNNING) {
                return false;
            }
            ShellCommand.Wait();
            ExitStatus = *ShellCommand.GetExitCode();
            return true;
        }

    private:
        TFile StdoutFile;
        TFile StderrFile;
        TUnbufferedFileOutput StdoutStream;
        TUnbufferedFileOutput StderrStream;
        const TString StopEventName;
        const TFileHandle StopEventHandle;
        TShellCommand ShellCommand;
    };
#endif

    void TShellCommandResult::SetLast(const TIntrusivePtr<TShellCommandResult>& command) {
        with_lock(Lock) {
            Last = command;
        }
    }

    TIntrusivePtr<TShellCommandResult> TShellCommandResult::GetLast() {
        with_lock(Lock) {
            return Last;
        }
    }

    void TShellCommandResult::ResetLast() {
        with_lock(Lock) {
            Last = nullptr;
        }
    }

    TString TShellCommandResult::Describe() const {
        return Sprintf("%s, exit code [%d], stdout [%s], stderr [%s]",
                       CmdDesc.c_str(), ExitCode, StdOut.c_str(), StdErr.c_str());
    }

    TString TShellCommandResult::CheckOk() const {
        if (ExitCode != 0) {
            Cout << Sprintf("unexpected exit code [%d], stdout [%s], stderr [%s]",
                            ExitCode,
                            StdOut.c_str(),
                            StdErr.c_str())
                 << Endl;
        }
        Y_ENSURE(ExitCode == 0, Sprintf("unexpected exit code [%d]", ExitCode));
        return StdOut;
    }

    TIntrusivePtr<TShellCommandResult> TShellCommandResult::Last;
    TAdaptiveLock TShellCommandResult::Lock;

    THolder<IOSProcess> MakeOSProcess(const TString& cmd,
        const TVector<TString>& args, const THashMap<TString, TString>& env,
        const TString& stdoutFilePath, const TString& stderrFilePath)
    {
        auto result = MakeHolder<TOSProcess>(cmd, args, env, stdoutFilePath, stderrFilePath);
        Cout << Sprintf("%s started [%s], args [%s], env [%s], pid [%u]",
            ToString(Now()).c_str(),
            cmd.c_str(),
            JoinSeq(" ", args).c_str(),
            JoinSeq(",", env, [](const auto& p) {
                return Sprintf("%s=%s", p.first.c_str(), p.second.c_str());
            }).c_str(),
            result->GetPid())
             << Endl;
        return result;
    }

    TIntrusivePtr<TShellCommandResult> ExecuteCommand(const TString& cmd,
        const TVector<TString>& args,
        const THashMap<TString, TString>& env,
        const TDuration& timeout)
    {
        TTempFileHandle stdoutFile{};
        TTempFileHandle stderrFile{};
        auto processPtr = MakeOSProcess(cmd, args, env, stdoutFile.GetName(), stderrFile.GetName());
        processPtr->Wait(timeout);

        auto result = MakeIntrusive<TShellCommandResult>();
        result->CmdDesc = Sprintf("cmd [%s], args [%s], pid [%d]",
                                  TString(cmd).c_str(),
                                  JoinSeq(" ",args).c_str(),
                                  processPtr->GetPid());
        result->ExitCode = processPtr->GetExitStatus();
        result->StdOut = TUnbufferedFileInput(stdoutFile).ReadAll();
        result->StdErr = TUnbufferedFileInput(stderrFile).ReadAll();
        SubstGlobal(result->StdOut, "\r\n", "\n");
        SubstGlobal(result->StdErr, "\r\n", "\n");
        TShellCommandResult::SetLast(result);

        return result;
    }

    // stolen from devtools/executor/lib/proc_util.cpp
    void EnsureSubreaperInitialized() {
#ifdef _win_
        static THolder<TFileHandle> subreaperHandle{nullptr};
        static std::once_flag subreaperHandleInitialized{};

        std::call_once(subreaperHandleInitialized, []() {
            HANDLE jobHandle = CreateJobObject(nullptr, nullptr);
            if (!jobHandle) {
                return;
            }
            subreaperHandle = MakeHolder<TFileHandle>(jobHandle);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo = {};
            jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(jobHandle, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo))) {
                return;
            }
            if (!AssignProcessToJobObject(jobHandle, GetCurrentProcess())) {
                return;
            }
        });
#endif
    }
}
