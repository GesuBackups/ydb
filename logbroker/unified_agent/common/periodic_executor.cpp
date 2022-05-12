#include "periodic_executor.h"

#include <logbroker/unified_agent/common/datetime.h>
#include <logbroker/unified_agent/common/future.h>
#include <logbroker/unified_agent/common/util/clock.h>

#include <util/random/random.h>

namespace NUnifiedAgent {
    using namespace NThreading;

    THalfJitter::THalfJitter()
        : Rng_{::MicroSeconds()}
    {
    }

    THalfJitter::THalfJitter(ui64 seed)
        : Rng_{seed}
    {
    }

    TDuration THalfJitter::Apply(TDuration delay) {
        static const auto maxDelay = TDuration::MicroSeconds(Max<ui32>());
        if (delay > maxDelay) {
            delay = maxDelay;
        }
        const auto half = delay / 2;
        const auto randomUs = Rng_.Uniform(static_cast<ui32>(half.MicroSeconds()));
        return half + TDuration::MicroSeconds(randomUs);
    }

    TSymJitter::TSymJitter(TDuration max)
        : Max(max)
    {
    }

    TDuration TSymJitter::Apply(TDuration period) {
        const auto max = Max > period ? period : Max;
        const auto randomUs = RandomNumber<ui64>(max.MicroSeconds() * 2);
        return period - max + TDuration::MicroSeconds(randomUs);
    }

    TPeriodicExecutor::TPeriodicExecutor(TPollCallback&& callback,
                                         TPollingParameters&& parameters,
                                         TDelayedExecutor& delayedExecutor,
                                         TTaskExecutor& taskExecutor,
                                         TScopeLogger& logger)
        : Callback(std::move(callback))
        , Parameters(std::move(parameters))
        , DelayedExecutor(delayedExecutor)
        , TaskExecutor(taskExecutor)
        , RegisteredTask(nullptr)
        , PendingExecution(MakeFuture())
        , Started(false)
        , Executing(false)
        , TriggerTime(Nothing())
        , Period(Parameters.Period_)
        , Lock()
        , Timer([this]() {
            Y_VERIFY(RegisteredTask != nullptr);
            RegisteredTask->Pulse();
          }, Parameters.Name_)
        , Logger(logger.Child(Parameters.Name_))
    {
    }

    TPeriodicExecutor::~TPeriodicExecutor() {
        try {
            Stop().Wait();
        } catch(...) {
            Y_FAIL("stop error: %s", CurrentExceptionMessage().c_str());
        }
    }

    void TPeriodicExecutor::Start() {
        TInstant triggerTime;
        with_lock(Lock) {
            Y_VERIFY(!Started);
            Started = true;
            TriggerTime = triggerTime = Schedule(Parameters.Jitter_ != nullptr ? Period : TDuration::Zero());
            RegisteredTask = TaskExecutor.RegisterSingle(Parameters.Name_, Parameters.TaskPriority_, *this);
            DelayedExecutor.SetTimer(Timer, triggerTime);
        }
        YLOG_INFO(Sprintf("initial iteration is scheduled for [%s]", ToString(triggerTime).c_str()));
    }

    TFuture<void> TPeriodicExecutor::Stop() {
        with_lock(Lock) {
            if (!Started) {
                return MakeFuture();
            }
            Started = false;
            DelayedExecutor.ResetTimer(Timer);
        }
        YLOG_INFO("stopping");
        return RegisteredTask->Unregister()
            .Apply([this](const auto&) { return PendingExecution; })
            .Apply([this](const auto&) { YLOG_INFO("stopped"); });
    }

    void TPeriodicExecutor::TriggerOutOfBand(TInstant triggerTime) {
        with_lock(Lock) {
            if (!Started) {
                return;
            }
            TriggerTime = triggerTime;
            if (!Executing) {
                DelayedExecutor.SetTimer(Timer, *TriggerTime);
            }
        }
        YLOG_DEBUG(Sprintf("out of band iteration is scheduled for [%s]", ToString(triggerTime).c_str()));
    }

    void TPeriodicExecutor::SetPeriod(TDuration period) {
        with_lock(Lock) {
            if (Period == period) {
                return;
            }
            Period = period;
        }
        YLOG_DEBUG(Sprintf("new period [%s] is set", ToString(period).c_str()));
    }

    TInstant TPeriodicExecutor::Schedule(TDuration delay) {
        // Lock must be held

        if (Parameters.Jitter_ != nullptr && delay > TDuration::Zero()) {
            delay = Parameters.Jitter_->Apply(delay);
        }
        const auto now = TClock::Now();
        auto result = now + delay;
        if (Parameters.AlignToGrid_) {
            result = AlignToGrid(Period, now, result);
        }
        return result;
    }

    bool TPeriodicExecutor::Run() {
        TFMaybe<TInstant> currentTriggerTime;
        with_lock(Lock) {
            if (!Started || Executing) {
                return false;
            }
            if (TriggerTime.Defined() && *TriggerTime > TClock::Now()) {
                DelayedExecutor.SetTimer(Timer, *TriggerTime);
                return false;
            }
            Executing = true;
            currentTriggerTime = TriggerTime;
            TriggerTime.Clear();
        }

        if (!currentTriggerTime.Defined()) {
            currentTriggerTime = TClock::Now();
        }
        PendingExecution = Callback(*currentTriggerTime).Apply([this](const auto& f) {
            const auto result = ExtractValue(f);
            if (!result.Ok()) {
                Y_VERIFY(Parameters.ExceptionsBackoff_ != nullptr,
                    "periodic executor [%s], unexpected exception [%s]",
                    Parameters.Name_.c_str(), result.Error->c_str());
                YLOG_ERR(Sprintf("iteration failed, exception [%s]", result.Error->c_str()));
            }

            TInstant triggerTime;
            with_lock(Lock) {
                Executing = false;
                if (result.Ok() && Parameters.ExceptionsBackoff_ != nullptr) {
                    Parameters.ExceptionsBackoff_->Reset();
                }

                if (!Started) {
                    return;
                }
                if (!TriggerTime) {
                    const auto delay = result.Ok()
                        ? Period
                        : Max(Parameters.ExceptionsBackoff_->Next(), Period);
                    TriggerTime = Schedule(delay);
                }
                triggerTime = *TriggerTime;
                DelayedExecutor.SetTimer(Timer, *TriggerTime);
            }
            YLOG_DEBUG(Sprintf("next iteration is scheduled for [%s]", ToString(triggerTime).c_str()));
        });

        return false;
    }
}
