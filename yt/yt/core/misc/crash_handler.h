#pragma once

#include <yt/yt/core/misc/optional.h>

#include <util/generic/string.h>

#include <set>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

// Dumps signal, stack frame information and codicils.
void CrashSignalHandler(int signal, siginfo_t* si, void* uc);

void DumpCodicils();

template <class TCallback>
void DumpStackTrace(TCallback flushCallback);

////////////////////////////////////////////////////////////////////////////////

// "Codicils" are short human- and machine-readable strings organized into a per-fiber stack.
// When the crash handler is invoked, it dumps (alongside with the other
// useful stuff like backtrace) the content of the latter stack.

//! Installs a new codicil into the stack.
void PushCodicil(const TString& data);
//! Removes the top codicils from the stack.
void PopCodicil();

//! Invokes #PushCodicil in ctor and #PopCodicil in dtor.
class TCodicilGuard
{
public:
    TCodicilGuard();
    explicit TCodicilGuard(const TString& data);
    ~TCodicilGuard();

    TCodicilGuard(const TCodicilGuard& other) = delete;
    TCodicilGuard(TCodicilGuard&& other);

    TCodicilGuard& operator=(const TCodicilGuard& other) = delete;
    TCodicilGuard& operator=(TCodicilGuard&& other);


private:
    bool Active_;

    void Release();

};

//! Writes the given buffer with the length to the standard error.
void WriteToStderr(const char* buffer, int length);
//! Writes the given zero-terminated buffer to the standard error.
void WriteToStderr(const char* buffer);
//! Same for TStringBuf.
void WriteToStderr(TStringBuf buffer);
//! Same for TString.
void WriteToStderr(const TString& buffer);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

#define CRASH_HANDLER_INL_H_
#include "crash_handler-inl.h"
#undef CRASH_HANDLER_INL_H_
