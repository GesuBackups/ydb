#pragma once

#include <util/generic/fwd.h>
#include <util/system/event.h>

#include <functional>

namespace NUnifiedAgent {
    void SetShutdownSignalHandler();

    void ResetShutdownSignalHandler();

    void SetFatalSignalHandler();

    bool WaitForShutdown(const TDuration& timeout = TDuration::Max(), bool silent = false);

    TManualEvent& GetShutdownEvent();

    void TriggerShutdown();
}
