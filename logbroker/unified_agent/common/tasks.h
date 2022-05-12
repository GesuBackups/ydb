#pragma once

#include <logbroker/unified_agent/common/util/async_joiner.h>
#include <logbroker/unified_agent/common/util/duration_counter.h>
#include <logbroker/unified_agent/common/util/dynamic_counters_wrapper.h>

#include <library/cpp/threading/future/future.h>

#include <util/generic/deque.h>
#include <util/generic/vector.h>
#include <util/system/condvar.h>
#include <util/system/mutex.h>
#include <util/system/thread.h>
#include <util/generic/hash.h>

#include <thread>

namespace NUnifiedAgent {
    class ITask {
    public:
        virtual ~ITask() = default;

        virtual bool Run() = 0;
    };

    enum class ETaskActivationMode {
        Single,
        Concurrent
    };

    struct TSpecificTaskCounters: public NMonitoring::TDynamicCounters {
        TSpecificTaskCounters();

        void Update();

        NMonitoring::TDeprecatedCounter& Runs;
        TDurationUsCounter RunDurationUs;
    };

    struct TTaskExecutorCounters: public TUpdatableCounters {
        explicit TTaskExecutorCounters(const TIntrusivePtr<NMonitoring::TDynamicCounters>& counters);

        TSpecificTaskCounters& GetSpecificTaskCounters(const TString& taskName);

        void Update() override;

    private:
        THashMap<TString, TSpecificTaskCounters*> SpecificTaskCounters;
        TAdaptiveLock Lock;
    };

    class TTaskHandle;

    enum class ETaskPriority {
        Normal,
        Urgent
    };

    class TTaskExecutor {
    public:
        TTaskExecutor(size_t threadsCount,
                      TDuration schedulePeriod,
                      const TString& threadName = "task_executor",
                      const TIntrusivePtr<NMonitoring::TDynamicCounters>& counters =
                          MakeIntrusive<NMonitoring::TDynamicCounters>());

        ~TTaskExecutor();

        void Start();

        void Stop();

        TIntrusivePtr<TTaskHandle> Register(const TString& taskName,
                                            ETaskPriority taskPriority,
                                            const std::function<THolder<ITask>()>& createTask,
                                            ETaskActivationMode taskActivationMode = ETaskActivationMode::Concurrent);

        TIntrusivePtr<TTaskHandle> RegisterSingle(const TString& taskName, ETaskPriority taskPriority, ITask& task);

        static TTaskHandle* CurrentTask();

        static TTaskHandle& CurrentTaskOrDie();

        inline const TIntrusivePtr<TTaskExecutorCounters>& GetCounters() const noexcept {
            return Counters;
        }

        friend class TTaskHandle;

    private:
        struct TWorker;

    private:
        void Run(TWorker* worker) noexcept;

        void TryToOffloadOnDormantWorker(TWorker* worker);

        TIntrusivePtr<TTaskHandle> CreateTaskHandle(const TString& taskName);

    private:
        TVector<TWorker> Workers;
        TMutex Lock;
        std::atomic<bool> Started;
        ui64 SchedulePeriod;
        TString ThreadName;
        TIntrusivePtr<TTaskExecutorCounters> Counters;
    };

    class TTaskHandle final: public TAtomicRefCount<TTaskHandle> {
    public:
        TTaskHandle(const TString& taskName, TSpecificTaskCounters& counters, TTaskExecutor& executor);

        ~TTaskHandle();

        // Join ExecutionJoiner, waits for pending tasks to complete.
        NThreading::TFuture<void> Unregister();

        // Schedules the task for execution.
        // Can be safely called until ExecutionJoiner is completed.
        // If wakeUp is true, try to wake up dormant worker to execute the task ASAP.
        void Pulse(bool wakeUp = false);

        // Add delayed action to execute when the main task iteration is done.
        // Can be called only from this task.
        void AddFinishAction(std::function<void()>&& action);

        inline TAsyncJoiner& ExecutionJoiner() noexcept {
            return ExecutionJoiner_;
        }

        friend class TTaskExecutor;

    private:
        struct TWorkItem;

        class TLocalWorkItemQueue;

        class TLocalWorkItemQueueImpl;

    private:
        TString TaskName;
        TSpecificTaskCounters& Counters;
        TTaskExecutor& Executor;
        std::function<THolder<ITask>()> CreateTask;
        TVector<THolder<TWorkItem>> WorkItems;
        TAsyncJoiner ExecutionJoiner_;
        bool Unregistered;
    };
}
