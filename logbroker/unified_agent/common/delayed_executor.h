#pragma once

#include <logbroker/unified_agent/common/util/dynamic_counters_wrapper.h>
#include <logbroker/unified_agent/common/util/duration_counter.h>
#include <logbroker/unified_agent/common/util/logger.h>

#include <robot/library/priorityqueue/priorityqueue.h>

#include <util/generic/ptr.h>
#include <util/datetime/base.h>
#include <util/system/condvar.h>
#include <util/system/mutex.h>
#include <util/system/spinlock.h>

#include <thread>

namespace NUnifiedAgent {
    class TTimer {
    public:
        TTimer(const std::function<void()>& callback, const TString& name);

    private:
        friend class TDelayedExecutor;

        std::function<void()> Callback;
        ui32 Handle;
        TInstant TriggerTime;
        TString Name;
    };

    struct TDelayedExecutorCounters: public TUpdatableCounters {
        explicit TDelayedExecutorCounters(const TIntrusivePtr<NMonitoring::TDynamicCounters>& counters);

        void Update() override;

        NMonitoring::TDeprecatedCounter& ExecutedCallbacks;
        NMonitoring::TDeprecatedCounter& OverdueUs;
        NMonitoring::TDeprecatedCounter& QueueSize;
        TDurationUsCounter ExecutionDurationUs;
    };

    class TDelayedExecutor {
    public:
        TDelayedExecutor(TScopeLogger& logger,
                         const TIntrusivePtr<NMonitoring::TDynamicCounters>& counters =
                             MakeIntrusive<NMonitoring::TDynamicCounters>());

        ~TDelayedExecutor();

        void Start();

        void Stop();

        void SetTimer(TTimer& timer, TInstant triggerTime);

        void ResetTimer(TTimer& timer);

        inline const TIntrusivePtr<TDelayedExecutorCounters>& GetCounters() const noexcept {
            return Counters;
        };

        void WakeUp();

    private:
        struct TQueueItem {
            TTimer* Target;

            TInstant GetPriority() const {
                return Target->TriggerTime;
            }

            void SetPriority(TInstant newTriggerTime) {
                Target->TriggerTime = newTriggerTime;
            }

            void SetIndex(ui32 newIndex) {
                Target->Handle = newIndex;
            }
        };

    private:
        void RunSleeper();

    private:
        TScopeLogger Logger;
        TIntrusivePtr<TDelayedExecutorCounters> Counters;
        NRobot::TUpdatablePriorityQueue<TQueueItem> Queue;
        std::thread Sleeper;
        bool Started;
        TTimer* ExecutingTimer;
        bool PendingReset;
        TCondVar WakeUpSleeper;
        TCondVar WakeUpReset;
        bool Debug;
        TMutex Lock;
    };

    class TLocalTimersQueue {
    public:
        class TDeadline {
        public:
            TDeadline();

            inline size_t& Data() {
                return Data_;
            }

            inline TInstant Value() const {
                return Value_;
            }

            inline bool IsSet() const {
                return Value_ != TInstant::Max();
            }

        private:
            friend class TLocalTimersQueue;

        private:
            size_t Data_;
            TInstant Value_;
            ui32 Handle_;
        };

    public:
        TLocalTimersQueue(const std::function<void()>& callback,
                          const TString& name,
                          TDelayedExecutor& delayedExecutor);

        void SetTimer(TDeadline& timer, TInstant triggerTime);

        void ResetTimer(TDeadline& timer);

        void Finalize();

        inline TDeadline& Top() noexcept {
            return *Queue.Top().Target;
        }

        inline bool Empty() const noexcept {
            return Queue.GetCount() == 0;
        }

        inline bool IsFinalized() const noexcept {
            return Finalized;
        }

    private:
        void CommitTimer();

        void EnsureCommitTimerScheduled();

    private:
        struct TQueueItem {
            TDeadline* Target;

            TInstant GetPriority() const {
                return Target->Value_;
            }

            void SetPriority(TInstant newTriggerTime) {
                Target->Value_ = newTriggerTime;
            }

            void SetIndex(ui32 newIndex) {
                Target->Handle_ = newIndex;
            }
        };

    private:
        ::NUnifiedAgent::TTimer Timer;
        TDelayedExecutor& DelayedExecutor;
        TFMaybe<TInstant> TimerTriggerTime;
        bool CommitTimerScheduled;
        bool Finalized;
        NRobot::TUpdatablePriorityQueue<TQueueItem> Queue;
    };
}
