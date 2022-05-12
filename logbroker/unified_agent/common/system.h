#pragma once

#ifdef _unix_
#include <unistd.h>
#endif

#include <sys/types.h>

#include <util/generic/fwd.h>
#include <util/system/getpid.h>
#include <util/network/init.h>

#include <cstdio>

namespace NUnifiedAgent {
    ssize_t DoRecvFrom(int socket, void* buffer, size_t length);

    void UnlinkOrThrow(const char* path);

    void Terminate(TProcessId pid);

    void DisableBuffer(FILE* f);

    void DoClose(SOCKET& fd);
#ifdef _linux_
    pid_t ForkSlave();
#endif

#ifdef _unix_
    ssize_t DoRead(int fd, void* buffer, size_t byteCount);

    ssize_t DoWrite(int fd, const void* buf, size_t count);

    pid_t DoFork();

    pid_t DoWaitPid(pid_t pid, int* wstatus, int options);

    void Exec(const TString& command, const TVector<TString>& arguments);

    void DoKill(pid_t pid, int sig);

#endif
}
