#pragma once

#include <logbroker/unified_agent/common/util/logger.h>

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/system/mutex.h>

#define REACH_HOOK(name)                           \
    do {                                           \
        if (TFailureInjector::Enabled()) {         \
            TFailureInjector::ReachHook(name);     \
        }                                          \
    } while (false)

namespace NUnifiedAgent {
    class TFailureInjector {
    public:
        struct TErrorAction {
            TErrorAction() = default;
            explicit TErrorAction(TString message, int signal = 0)
                : Message(std::move(message))
                , ExitSignal(signal) {
            }
            TString Message;
            int ExitSignal;
        };

        struct TDelayAction {
            TDuration Duration;
        };

        using TAction = std::variant<TErrorAction, TDelayAction>;

    public:
        static void SetHook(const TString& name, ui64 skipVisits, ui64 triggerCount, TAction&& action);

        static void ReachHook(const TString& name);

        static bool ReachHook(const TString& name, TAction& action);

        static inline bool Enabled() noexcept {
            return Enabled_;
        }

        static void Configure(TScopeLogger& logger);

    private:
        struct THook {
            ui64 SkipVisits;
            ui64 TriggerCount;
            TAction Action;
        };

    private:
        static TScopeLogger Logger;
        static THashMap<TString, THook> Hooks;
        static TMutex Lock;
        static bool Enabled_;
    };
}
