#include "delayed_executor.h"

#include <logbroker/unified_agent/common/tasks.h>
#include <logbroker/unified_agent/common/util/clock.h>

#include <util/system/env.h>
#include <util/system/thread.h>

namespace NUnifiedAgent {
    namespace {
        static const auto OverdueWarningThreshold = TDuration::MilliSeconds(100);
    }

    TTimer::TTimer(const std::function<void()>& callback, const TString& name)
        : Callback(callback)
        , Handle(0)
        , TriggerTime(TInstant::Max())
        , Name(name)
    {
    }

    TDelayedExecutorCounters::TDelayedExecutorCounters(const TIntrusivePtr<NMonitoring::TDynamicCounters>& counters)
        : TUpdatableCounters(counters)
        , ExecutedCallbacks(GetCounter("ExecutedCallbacks", true))
        , OverdueUs(GetCounter("OverdueUs", true))
        , QueueSize(GetCounter("QueueSize", false))
        , ExecutionDurationUs("ExecutionDurationUs", *counters)
    {
    }

    void TDelayedExecutorCounters::Update() {
        ExecutionDurationUs.Update();
    }

    TDelayedExecutor::TDelayedExecutor(TScopeLogger& logger,
                                       const TIntrusivePtr<NMonitoring::TDynamicCounters>& counters)
        : Logger(logger.Child("delayed_executor"))
        , Counters(MakeIntrusive<TDelayedExecutorCounters>(counters))
        , Queue()
        , Sleeper()
        , Started(false)
        , ExecutingTimer(nullptr)
        , PendingReset(false)
        , WakeUpSleeper()
        , WakeUpReset()
        , Debug(!GetEnv("UA_DEBUG").empty())
        , Lock()
    {
    }

    TDelayedExecutor::~TDelayedExecutor() {
        try {
            Stop();
        } catch(...) {
            Y_FAIL("stop error: %s", CurrentExceptionMessage().c_str());
        }
    }

    void TDelayedExecutor::Start() {
        with_lock(Lock) {
            Y_VERIFY(!Started, "already started");
            Started = true;
        }
        Sleeper = std::thread(&TDelayedExecutor::RunSleeper, this);
    }

    void TDelayedExecutor::Stop() {
        with_lock(Lock) {
            if (!Started) {
                return;
            }
            Started = false;
            WakeUpSleeper.Signal();
        }
        Sleeper.join();
    }

    void TDelayedExecutor::SetTimer(TTimer& timer, TInstant triggerTime) {
        if (const auto now = TClock::Now(); triggerTime < now) {
            triggerTime = now;
        }

        with_lock(Lock) {
            Y_VERIFY(Started);
            if (timer.Handle == 0) {
                timer.TriggerTime = triggerTime;
                Queue.Enqueue({&timer});
                ++Counters->QueueSize;
            } else {
                Queue.Update(timer.Handle, triggerTime);
            }
            if (timer.Handle == 1) {
                WakeUpSleeper.Signal();
            }
        }
    }

    void TDelayedExecutor::ResetTimer(TTimer& timer) {
        with_lock(Lock) {
            Y_VERIFY(Started);
            while (ExecutingTimer == &timer) {
                PendingReset = true;
                WakeUpReset.WaitI(Lock);
                PendingReset = false;
            }
            if (timer.Handle != 0) {
                Queue.Remove(timer.Handle);
                --Counters->QueueSize;
            }
        }
    }

    void TDelayedExecutor::WakeUp() {
        with_lock(Lock) {
            WakeUpSleeper.Signal();
        }
    }

    void TDelayedExecutor::RunSleeper() {
        TThread::SetCurrentThreadName("ua_dexec");
        with_lock(Lock) {
            while (Started) {
                if (Queue.GetCount() == 0) {
                    WakeUpSleeper.WaitI(Lock);
                    continue;
                }
                auto* topTimer = Queue.Top().Target;
                const auto now = TClock::Now();
                if (topTimer->TriggerTime > now) {
                    WakeUpSleeper.WaitT(Lock, topTimer->TriggerTime - now);
                    continue;
                }
                Queue.Dequeue();
                --Counters->QueueSize;
                if (!Debug) {
                    const auto overdueDuration = now - topTimer->TriggerTime;
                    if (overdueDuration > OverdueWarningThreshold) {
                        YLOG_WARNING(Sprintf("timer [%s] overdue [%s], trigger time [%s], now [%s]",
                                             topTimer->Name.c_str(), ToString(overdueDuration).c_str(),
                                             ToString(topTimer->TriggerTime).c_str(), ToString(now).c_str()));
                    }
                    Counters->OverdueUs += overdueDuration.MicroSeconds();
                }
                ExecutingTimer = topTimer;
                {
                    auto u = Unguard(Lock);
                    TDurationUsCounter::TScope scope(Counters->ExecutionDurationUs);
                    try {
                        topTimer->Callback();
                    } catch (...) {
                        Y_FAIL("timer [%s] callback failed: %s",
                               topTimer->Name.c_str(), CurrentExceptionMessage().c_str());
                    }
                    ++Counters->ExecutedCallbacks;
                }
                ExecutingTimer = nullptr;
                if (PendingReset) {
                    WakeUpReset.BroadCast();
                }
            }
        }
    }

    TLocalTimersQueue::TDeadline::TDeadline()
        : Data_(0)
        , Value_(TInstant::Max())
        , Handle_(0)
    {
    }

    TLocalTimersQueue::TLocalTimersQueue(const std::function<void()>& callback,
                                         const TString& name,
                                         TDelayedExecutor& delayedExecutor)
        : Timer(callback, name)
        , DelayedExecutor(delayedExecutor)
        , TimerTriggerTime(Nothing())
        , CommitTimerScheduled(false)
        , Finalized(false)
        , Queue()
    {
    }

    void TLocalTimersQueue::SetTimer(TDeadline& deadline, TInstant triggerTime) {
        Y_VERIFY(!Finalized, "timer queue is already finalized");
        EnsureCommitTimerScheduled();

        if (deadline.Handle_ == 0) {
            deadline.Value_ = triggerTime;
            Queue.Enqueue({&deadline});
        } else {
            Queue.Update(deadline.Handle_, triggerTime);
        }
    }

    void TLocalTimersQueue::ResetTimer(TDeadline& deadline) {
        EnsureCommitTimerScheduled();

        if (deadline.Handle_ != 0) {
            Queue.Remove(deadline.Handle_);
        }
        deadline.Value_ = TInstant::Max();
    }

    void TLocalTimersQueue::Finalize() {
        EnsureCommitTimerScheduled();

        for (size_t i = 1; i <= Queue.GetCount(); ++i) {
            auto& t = *Queue.Get(i).Target;
            t.Handle_ = 0;
            t.Value_ = TInstant::Max();
        }
        Queue.Clear();

        Finalized = true;
    }

    void TLocalTimersQueue::CommitTimer() {
        if (Queue.GetCount() > 0) {
            const auto triggerTime = Top().Value();
            if (!TimerTriggerTime.Defined() || *TimerTriggerTime < TClock::Now() || triggerTime < *TimerTriggerTime) {
                if (!TimerTriggerTime.Defined()) {
                    TTaskExecutor::CurrentTaskOrDie().ExecutionJoiner().Ref();
                }
                DelayedExecutor.SetTimer(Timer, triggerTime);
                TimerTriggerTime = triggerTime;
            }
        } else if (TimerTriggerTime.Defined()) {
            DelayedExecutor.ResetTimer(Timer);
            TTaskExecutor::CurrentTaskOrDie().ExecutionJoiner().UnRef();
            TimerTriggerTime.Clear();
        }

        CommitTimerScheduled = false;
    }

    void TLocalTimersQueue::EnsureCommitTimerScheduled() {
        if (!CommitTimerScheduled) {
            TTaskExecutor::CurrentTaskOrDie().AddFinishAction([this]() { CommitTimer(); });
            CommitTimerScheduled = true;
        }
    }
}
