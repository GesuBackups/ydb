#include "system.h"

#include <util/network/sock.h>
#include <util/string/printf.h>
#include <cerrno>
#include <stdio.h>

#ifdef _linux_
#include <sys/prctl.h>
#endif

#ifdef _unix_
#include <util/generic/size_literals.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>
#include <util/network/init.h>
#include <util/system/error.h>

#include <sys/mman.h>
#include <sys/wait.h>
#endif

#ifdef _win_
#include "processthreadsapi.h"
#include "winsock.h"
#endif

namespace NUnifiedAgent {
#ifdef _linux_
    int DoPrctl(int option, unsigned long arg2) {
        const int result = ::prctl(option, arg2);
        Y_VERIFY(result != -1, "prctl failed, result [%d], errno [%d]", result, errno);
        return result;
    }

    void DeliverKillSignalOnParentDeath() {
        DoPrctl(PR_SET_PDEATHSIG, SIGKILL);
    }

    pid_t ForkSlave() {
        const pid_t result = DoFork();
        if (result == 0) {
            DeliverKillSignalOnParentDeath();
        }
        return result;
    }
#endif

#ifdef _unix_

    ssize_t DoRead(int fd, void* buffer, size_t byteCount) {
        while (true) {
            const ssize_t result = ::read(fd, buffer, byteCount);
            if (result == -1) {
                const auto error = LastSystemError();
                if (error == EINTR) {
                    continue;
                }
                if (error == EAGAIN) {
                    return 0;
                }
                Y_FAIL("read failed, handle [%d], errno [%d]", fd, error);
            }
            return result;
        }
    }

    ssize_t DoWrite(int fd, const void* buf, size_t count) {
        while (true) {
            const ssize_t result = ::write(fd, buf, count);
            if (result == -1) {
                const auto error = LastSystemError();
                if (error == EINTR) {
                    continue;
                }
                if (error == EAGAIN) {
                    return 0;
                }
                if (error == EPIPE) {
                    throw TSystemError(error);
                }
                Y_FAIL("write failed, handle [%d], errno [%d]", fd, error);
            }
            return result;
        }
    }

    pid_t DoFork() {
        const pid_t result = ::fork();
        Y_VERIFY(result != -1, "fork failed, result [%d], errno [%d]", result, errno);
        return result;
    }

    pid_t DoWaitPid(pid_t pid, int* wstatus, int options) {
        while (true) {
            const pid_t result = ::waitpid(pid, wstatus, options);
            if (result == -1) {
                if (errno == EINTR) {
                    continue;
                }
                Y_FAIL("waitpid failed, result [%d], errno [%d]", result, errno);
            }
            return result;
        }
    }

    void Exec(const TString& command, const TVector<TString>& arguments) {
        TVector<char*> args;
        args.reserve(arguments.size() + 2);
        args.push_back(const_cast<char*>(command.data()));
        for (const auto& i : arguments) {
            args.push_back(const_cast<char*>(i.data()));
        }
        args.push_back(nullptr);
        const auto execResult = ::execv(args[0], args.data());
        Y_FAIL("execv [%s] failed, result [%d], error code [%d], error text [%s]",
               command.c_str(), execResult, LastSystemError(), LastSystemErrorText());
    }

    void DoKill(pid_t pid, int sig) {
        const int result = ::kill(pid, sig);
        if (result == -1 && errno == ESRCH) {
            return;
        }
        Y_VERIFY(result == 0, "kill failed, result [%d], errno [%d]", result, errno);
    }
#endif

    ssize_t DoRecvFrom(int socket, void* buffer, size_t length) {
        while (true) {
            const ssize_t result = ::recvfrom(socket, static_cast<char*>(buffer), length, 0, nullptr, nullptr);
            if (result == -1) {
                const auto error = LastSystemError();
                if (error == EINTR) {
                    continue;
                }
                if (error == EAGAIN || error == EWOULDBLOCK) {
                    return 0;
                }
                Y_FAIL("read failed, socket [%d], error [%s]", socket, LastSystemErrorText());
            }
            return result;
        }
    }

    void UnlinkOrThrow(const char* path) {
        const auto result = ::unlink(path);
        Y_ENSURE(result == 0,
            Sprintf("unlink [%s] failed, result [%d], error code [%d], error text [%s]",
                path, result, LastSystemError(), LastSystemErrorText()));
    }

    void Terminate(TProcessId pid) {
#if defined(_unix_)
        DoKill(pid, SIGKILL);
#elif defined(_win_)
        const auto handle = OpenProcess(PROCESS_TERMINATE, false, pid);
        TerminateProcess(handle, 1);
        CloseHandle(handle);
#else
        Y_FAIL("unsupported platform");
#endif
    }

    void DisableBuffer(FILE* f) {
        const auto result = std::setvbuf(f, nullptr, _IONBF, 0);
        Y_VERIFY(result == 0, "unexpected setvbuf result [%d], errno [%d]", result, errno);
    }

    void DoClose(SOCKET& fd) {
        Y_VERIFY(fd != INVALID_SOCKET, "invalid handle");
        const int result = ::close(fd);
        if (result == -1) {
            const auto error = LastSystemError();
            Y_VERIFY(error == EINTR || error != EBADF, "close failed, errno [%d], fd [%d]", error, fd);
        }
        fd = INVALID_SOCKET;
    }
}
