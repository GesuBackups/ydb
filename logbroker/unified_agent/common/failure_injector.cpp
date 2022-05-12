#include "failure_injector.h"

#include <logbroker/unified_agent/common/system.h>
#include <logbroker/unified_agent/common/util/variant.h>

#include <util/system/getpid.h>

#include <cerrno>

namespace NUnifiedAgent {
    namespace {
        void GenerateSignal(int signal) {
            if (signal == SIGSEGV) {
                volatile int* p = nullptr;
                *p = 42;
            } else if (signal == SIGABRT) {
                abort();
            }
            Y_FAIL("can't generate signal: %d", signal);
        }
    }

    void TFailureInjector::Configure(TScopeLogger& logger) {
        with_lock(Lock) {
            Logger = logger.Child("failure_injector");
        }
        Enabled_ = true;
    }

    void TFailureInjector::SetHook(const TString& name, ui64 skipVisits, ui64 triggerCount, TAction&& action) {
        with_lock(Lock) {
            Y_VERIFY(triggerCount > 0, "failure [%s], trigger count must not be zero", name.c_str());
            const auto [it, inserted] = Hooks.emplace(name, THook {
                skipVisits,
                triggerCount,
                std::move(action)
            });
            Y_VERIFY(inserted, "failure [%s] already set", name.c_str());
        }
    }

    void TFailureInjector::ReachHook(const TString& name) {
        TAction action;
        if (ReachHook(name, action)) {
            Visit(action,
                [&](TErrorAction& a) {
                    YLOG_INFO(Sprintf("hook [%s] reached, kill myself, message [%s]",
                        name.c_str(), a.Message.c_str()));
                    if (a.ExitSignal == 0) {
                        Terminate(GetPID());
                        return;
                    }
                    GenerateSignal(a.ExitSignal);
                },
                [&](TDelayAction& a) {
                    YLOG_INFO(Sprintf("hook [%s] reached, sleeping for [%s]",
                        name.c_str(), ToString(a.Duration).c_str()));
                    Sleep(a.Duration);
                    YLOG_INFO("woke up");
                });
        }
    }

    bool TFailureInjector::ReachHook(const TString& name, TAction& action) {
        with_lock(Lock) {
            const auto it = Hooks.find(name);
            if (it == Hooks.end()) {
                return false;
            }
            auto& failure = it->second;
            if (failure.SkipVisits > 0) {
                --failure.SkipVisits;
                return false;
            }
            action = failure.Action;
            Y_VERIFY(failure.TriggerCount > 0);
            --failure.TriggerCount;
            if (failure.TriggerCount == 0) {
                Hooks.erase(it);
            }
            return true;
        }
    }

    TScopeLogger TFailureInjector::Logger{};
    THashMap<TString, TFailureInjector::THook> TFailureInjector::Hooks{};
    TMutex TFailureInjector::Lock{};
    bool TFailureInjector::Enabled_{false};
}
