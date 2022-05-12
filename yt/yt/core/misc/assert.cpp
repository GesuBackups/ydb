#include "assert.h"

#include "crash_handler.h"
#include "raw_formatter.h"
#include "safe_assert.h"
#include "proc.h"

#include <yt/yt/core/misc/core_dumper.h>
#include <yt/yt/core/misc/string.h>

#include <yt/yt/core/concurrency/async_semaphore.h>

#include <yt/yt/core/logging/log_manager.h>

#include <library/cpp/yt/assert/assert.h>

#ifdef _win_
    #include <io.h>
#else
    #include <unistd.h>
#endif

#include <errno.h>

namespace NYT::NDetail {

using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

void AssertTrapImpl(
    TStringBuf trapType,
    TStringBuf expr,
    TStringBuf file,
    int line,
    TStringBuf function)
{
    TRawFormatter<1024> formatter;
    formatter.AppendString(trapType);
    formatter.AppendString("(");
    formatter.AppendString(expr);
    formatter.AppendString(") at ");
    formatter.AppendString(file);
    formatter.AppendString(":");
    formatter.AppendNumber(line);
    if (function) {
        formatter.AppendString(" in ");
        formatter.AppendString(function);
        formatter.AppendString("\n");
    }

    if (SafeAssertionsModeEnabled()) {
        auto semaphore = GetSafeAssertionsCoreSemaphore();
        std::optional<TString> corePath;
        if (auto semaphoreGuard = TAsyncSemaphoreGuard::TryAcquire(semaphore)) {
            try {
                std::vector<TString> coreNotes{"Reason: SafeAssertion"};
                auto contextCoreNotes = GetSafeAssertionsCoreNotes();
                coreNotes.insert(coreNotes.end(), contextCoreNotes.begin(), contextCoreNotes.end());
                auto coreDump = GetSafeAssertionsCoreDumper()->WriteCoreDump(coreNotes, "safe_assertion");
                corePath = coreDump.Path;
                // A tricky way to return slot only after core is written.
                coreDump.WrittenEvent.Subscribe(BIND([_ = std::move(semaphoreGuard)] (const TError&) { }));
            } catch (const std::exception&) {
                // Do nothing.
            }
        }
        TStringBuilder stackTrace;
        DumpStackTrace([&stackTrace] (TStringBuf str) {
            stackTrace.AppendString(str);
        });
        TString expression(formatter.GetData(), formatter.GetBytesWritten());
        throw TAssertionFailedException(std::move(expression), stackTrace.Flush(), std::move(corePath));
    } else {
        HandleEintr(::write, 2, formatter.GetData(), formatter.GetBytesWritten());

        NLogging::TLogManager::Get()->Shutdown();

        YT_BUILTIN_TRAP();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDetail
