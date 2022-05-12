#ifndef CRASH_HANDLER_INL_H_
#error "Direct inclusion of this file is not allowed, include crash_handler-inl.h"
// For the sake of sane code completion.
#include "crash_handler-inl.h"
#endif

#include "stack_trace.h"

#include <yt/yt/core/libunwind/libunwind.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

#ifdef _unix_

template <class TCallback>
void DumpStackTrace(TCallback writeCallback)
{
    // Get the stack trace (without current frame hence +1).
    std::array<void*, 99> frames; // 99 is to keep formatting. :)
    int frameCount = NLibunwind::GetStackTrace(frames.data(), frames.size(), 1);
    FormatStackTrace(frames.begin(), frameCount, writeCallback);
}

#else

template <class TCallback>
void DumpStackTrace(TCallback /*writeCallback*/)
{
    TRawFormatter<256> formatter;
    formatter.AppendString("(stack trace is not available for this platform)");
    flushCallback(formatter.GetData(), formatter.GetBytesWritten());
}

#endif

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
