#include "tasks.h"

#include <logbroker/unified_agent/common/mpsc_queue.h>
#include <logbroker/unified_agent/common/util/f_maybe.h>

#include <util/datetime/base.h>
#include <util/datetime/cputimer.h>
#include <util/generic/algorithm.h>
#include <util/generic/intrlist.h>
#include <util/system/type_name.h>
#include <util/system/types.h>

#ifdef _linux_
#include <linux/futex.h>
#include <sys/syscall.h>
#endif

#include <cerrno>

using namespace NThreading;
using namespace NMonitoring;

namespace NUnifiedAgent {
#ifdef _linux_
    namespace {
        inline int futex(
            int* uaddr, int op, int val, const timespec* timeout,
            int* uaddr2, int val3)
        {
            return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
        }

        void DoFutexWait(int& futexWord, int value) {
            const int result = futex(&futexWord, FUTEX_WAIT_PRIVATE, value, nullptr, nullptr, 0);
            Y_VERIFY(result != -1 || errno == EAGAIN || errno == EINTR,
                     "futex wait failed, result [%d], errno [%d]", result, errno);
        }

        void DoFutexWake(int& futexWord, int maxWaiters) {
            const int result = futex(&futexWord, FUTEX_WAKE_PRIVATE, maxWaiters, nullptr, nullptr, 0);
            Y_VERIFY(result != -1, "futex wake failed, result [%d], errno [%d]", result, errno);
        }
    }
#endif

    struct TTaskHandle::TWorkItem: public TMpscQueue::TNode {
        TWorkItem(TTaskHandle* taskHandle, ETaskPriority taskPriority, size_t index)
            : Revision(0)
            , LastWorker(nullptr)
            , LocalNext(nullptr)
            , TaskPriority(taskPriority)
            , OwnedTask(nullptr)
            , TaskToExecute(nullptr)
            , TaskHandle(taskHandle)
            , Unregister(NewPromise())
            , Index(index)
            , Pulsed(false)
        {
        }

        void SetTaskToExecute(ITask& task) {
            TaskToExecute = &task;
        }

        std::atomic<ui64> Revision;
        TTaskExecutor::TWorker* LastWorker;

        TWorkItem* LocalNext;
        ETaskPriority TaskPriority;

        THolder<ITask> OwnedTask;
        ITask* TaskToExecute;
        TTaskHandle* TaskHandle;
        TPromise<void> Unregister;
        size_t Index;
        bool Pulsed;
    };

    class TTaskHandle::TLocalWorkItemQueueImpl {
    public:
        TLocalWorkItemQueueImpl()
            : Sentinel(nullptr, ETaskPriority::Normal, 0)
            , Head(&Sentinel)
            , Tail(&Sentinel)
        {
        }

        void Enqueue(TWorkItem* item) {
            Tail->LocalNext = item;
            Tail = item;
        }

        TWorkItem* Dequeue() {
            auto* next = Head->LocalNext;
            if (next != nullptr) {
                if (Tail == next) {
                    Tail = Head;
                }
                Head->LocalNext = next->LocalNext;
                next->LocalNext = nullptr;
            }
            return next;
        }

    private:
        TWorkItem Sentinel;
        TWorkItem* Head;
        TWorkItem* Tail;
    };

    class TTaskHandle::TLocalWorkItemQueue {
    public:
        TLocalWorkItemQueue()
            : UrgentQueue()
            , NormalQueue()
        {
        }

        void Enqueue(TWorkItem* item) {
            auto& queue = item->TaskPriority == ETaskPriority::Urgent ? UrgentQueue : NormalQueue;
            queue.Enqueue(item);
        }

        TWorkItem* Dequeue() {
            auto* urgentItem = UrgentQueue.Dequeue();
            return urgentItem == nullptr ? NormalQueue.Dequeue() : urgentItem;
        }

    private:
        TLocalWorkItemQueueImpl UrgentQueue;
        TLocalWorkItemQueueImpl NormalQueue;
    };

    struct TTaskExecutor::TWorker {
        TMpscQueue ExternalQueue;
        TTaskHandle::TLocalWorkItemQueue LocalQueue;
        std::thread Thread;
        TVector<TTaskHandle::TWorkItem*> Buffer;
        std::atomic<ui64> Ticket;
#ifndef _linux_
        TCondVar Wakeup;
        TMutex WakeUpLock;
#endif
        TTaskExecutor* Executor;
        TTaskHandle::TWorkItem StopWorkItem;
        TVector<std::function<void()>> TaskFinishActions;
        TTaskHandle::TWorkItem* ExecutingItem;

        TWorker()
            : ExternalQueue()
            , LocalQueue()
            , Thread()
            , Buffer()
            , Ticket(0)
#ifndef _linux_
            , Wakeup()
            , WakeUpLock()
#endif
            , Executor(nullptr)
            , StopWorkItem(nullptr, ETaskPriority::Normal, 0)
            , TaskFinishActions()
            , ExecutingItem(nullptr)
        {
        }

        bool TryEnqueueExternal(TTaskHandle::TWorkItem& workItem) {
#ifndef _linux_
            auto guard = TGuard<TMutex>(WakeUpLock);
#endif
            auto workerTicket = Ticket.load();
            do {
                if (workerTicket == 0) {
                    return false;
                }
            } while(!Ticket.compare_exchange_weak(workerTicket, workerTicket + 1));
            workItem.LastWorker = this;
            ExternalQueue.Enqueue(&workItem);
#ifndef _linux_
            Wake();
#endif
            return true;
        }

        bool TryEnqueueDormantExternal(TTaskHandle::TWorkItem& workItem) {
            ui64 dormant = 0;
#ifndef _linux_
            auto guard = TGuard<TMutex>(WakeUpLock);
#endif
            if (!Ticket.compare_exchange_strong(dormant, 1)) {
                return false;
            }
            workItem.LastWorker = this;
            ExternalQueue.Enqueue(&workItem);
            Wake();
            return true;
        }

        void EnqueueExternalAndWake(TTaskHandle::TWorkItem& workItem) {
            while (true) {
                if (TryEnqueueDormantExternal(workItem) || TryEnqueueExternal(workItem)) {
                    return;
                }
            }
        }

        void Wake() {
#ifdef _linux_
            DoFutexWake(*(int*)(&Ticket), 1);
#else
            // this->WakeUpLock must be held
            Wakeup.Signal();
#endif
        }

        static thread_local TWorker* Current;
    };

    thread_local TTaskExecutor::TWorker* TTaskExecutor::TWorker::Current;

    TTaskHandle::TTaskHandle(const TString& taskName, TSpecificTaskCounters& counters, TTaskExecutor& executor)
        : TaskName(taskName)
        , Counters(counters)
        , Executor(executor)
        , CreateTask()
        , WorkItems()
        , ExecutionJoiner_()
        , Unregistered(false)
    {
    }

    TTaskHandle::~TTaskHandle() {
        Y_VERIFY(Unregistered, "task [%s] is not stopped correctly", TaskName.c_str());
    }

    void TTaskHandle::Pulse(bool wakeUp) {
        auto& workItem = *WorkItems.front();
        const auto revision = workItem.Revision.fetch_add(2);
        Y_VERIFY((revision & 1) == 0, "task already unregistered");
        if (revision > 0) {
            return;
        }
        workItem.Pulsed = true;

        if (auto* current = TTaskExecutor::TWorker::Current; current != nullptr && current->Executor == &Executor) {
            current->LocalQueue.Enqueue(&workItem);
            return;
        }

        if (wakeUp && Executor.Workers.size() > 0) {
            for (auto& w: Executor.Workers) {
                if (w.TryEnqueueDormantExternal(workItem)) {
                    return;
                }
            }
        }
        if (workItem.LastWorker && workItem.LastWorker->TryEnqueueExternal(workItem)) {
            return;
        }
        for (auto& w: Executor.Workers) {
            if (w.TryEnqueueExternal(workItem)) {
                return;
            }
        }
        Executor.Workers[0].EnqueueExternalAndWake(workItem);
    }

    TFuture<void> TTaskHandle::Unregister() {
        return ExecutionJoiner_.Join().Apply([this](const auto& f) {
            Y_VERIFY(f.HasValue());

            auto futures = TVector<TFuture<void>>();
            futures.reserve(WorkItems.size());
            for (auto& w: WorkItems) {
                futures.push_back(w->Unregister.GetFuture());
                const auto revision = w->Revision.fetch_add(1);
                Y_VERIFY((revision & 1) == 0, "task already unregistered");
                if (revision == 0) {
                    w->Unregister.SetValue();
                }
            }
            return WaitAll(futures);
        }).Apply([this](const auto& f) {
               Y_VERIFY(f.HasValue());

               Unregistered = true;
           });
    }

    void TTaskHandle::AddFinishAction(std::function<void()>&& action) {
        auto* currentWorker = TTaskExecutor::TWorker::Current;
        Y_VERIFY(currentWorker != nullptr, "must be called from task context");
        Y_VERIFY(currentWorker->ExecutingItem != nullptr, "no executing item");
        Y_VERIFY(currentWorker->ExecutingItem->TaskHandle == this, "unexpected task handle");
        currentWorker->TaskFinishActions.push_back(std::move(action));
    }

    TTaskExecutor::TTaskExecutor(size_t threadsCount,
                                 TDuration schedulePeriod,
                                 const TString& threadName,
                                 const TIntrusivePtr<TDynamicCounters>& counters)
        : Workers(threadsCount)
        , Lock()
        , Started(false)
        , SchedulePeriod(threadsCount == 1 ? 0 : schedulePeriod.MilliSeconds() * GetCyclesPerMillisecond())
        , ThreadName(threadName)
        , Counters(MakeIntrusive<TTaskExecutorCounters>(counters))
    {
    }

    TTaskExecutor::~TTaskExecutor() {
        try {
            Stop();
        } catch(...) {
            Y_FAIL("stop error: %s", CurrentExceptionMessage().c_str());
        }
    }

    void TTaskExecutor::Run(TWorker* worker) noexcept {
        TVector<std::function<void()>> taskFinishActions;
        TThread::SetCurrentThreadName(ThreadName.c_str());
        TWorker::Current = worker;
        ui64 nextSchedulePeriod = SchedulePeriod ? GetCycleCount() + SchedulePeriod : 0;
        ui64 receivedTickets = 0;
        ui64 spins = 0;
        while (true) {
            TTaskHandle::TWorkItem* workItem;
            while ((workItem = static_cast<TTaskHandle::TWorkItem*>(worker->ExternalQueue.Dequeue())) != nullptr) {
                worker->LocalQueue.Enqueue(workItem);
                ++receivedTickets;
            }
            if (nextSchedulePeriod && GetCycleCount() >= nextSchedulePeriod) {
                TryToOffloadOnDormantWorker(worker);
                nextSchedulePeriod = GetCycleCount() + SchedulePeriod;
            }
            workItem = worker->LocalQueue.Dequeue();
            if (workItem == &worker->StopWorkItem) {
                break;
            } else if (workItem == nullptr) {
                ui64 issuedTickets = worker->Ticket.load();
                if (receivedTickets < issuedTickets) {
                    SpinLockPause();
                } else if (spins < 100) {
                    SpinLockPause();
                    ++spins;
                } else {
#ifndef _linux_
                    auto wakeUpLockGuard = TGuard<TMutex>(worker->WakeUpLock);
#endif
                    if (worker->Ticket.compare_exchange_strong(issuedTickets, 0)) {
#ifdef _linux_
                        DoFutexWait(*(int*)(&worker->Ticket), 0);
#else
                        worker->Wakeup.WaitI(worker->WakeUpLock);
#endif
                        receivedTickets = 0;
                        nextSchedulePeriod = GetCycleCount() + SchedulePeriod;
                        spins = 0;
                    }
                }
                continue;
            }
            spins = 0;
            if (!workItem->TaskToExecute) {
                Y_VERIFY(!workItem->OwnedTask);
                workItem->OwnedTask = workItem->TaskHandle->CreateTask();
                workItem->SetTaskToExecute(*workItem->OwnedTask);
            }
            const auto execRevision = workItem->Revision.load();
            bool continueTask;
            try {
                worker->ExecutingItem = workItem;
                TDurationUsCounter::TScope scope(workItem->TaskHandle->Counters.RunDurationUs);
                continueTask = workItem->TaskToExecute->Run();
                while (!worker->TaskFinishActions.empty()) {
                    std::swap(taskFinishActions, worker->TaskFinishActions);
                    for (auto& a: taskFinishActions) {
                        a();
                    }
                    taskFinishActions.clear();
                }
                worker->ExecutingItem = nullptr;
            } catch (...) {
                Y_FAIL("task [%s] failed: %s",
                       workItem->TaskHandle->TaskName.c_str(),
                       CurrentExceptionMessage().c_str());
            }
            ++workItem->TaskHandle->Counters.Runs;

            auto revision = workItem->Revision.load();
            const auto pulsed = workItem->Pulsed;
            workItem->Pulsed = false;
            const auto enqueue = continueTask ||
                                 revision > execRevision + 1 ||
                                 (!workItem->Revision.compare_exchange_strong(revision, revision & 1) &&
                                  revision > execRevision + 1);
            if (enqueue) {
                worker->LocalQueue.Enqueue(workItem);
                auto& workItems = workItem->TaskHandle->WorkItems;
                if ((pulsed || revision > execRevision + 1) && workItem->Index < workItems.size() - 1) {
                    auto& nextWorkItem = *workItems[workItem->Index + 1];
                    if (nextWorkItem.Revision.fetch_add(2) == 0) {
                        nextWorkItem.Pulsed = true;
                        worker->LocalQueue.Enqueue(&nextWorkItem);
                    }
                }
            } else if ((revision & 1) == 1) {
                workItem->Unregister.SetValue();
            }
        }
    }

    void TTaskExecutor::TryToOffloadOnDormantWorker(TWorker* worker) {
        TTaskHandle::TWorkItem* workItem;
        while ((workItem = worker->LocalQueue.Dequeue()) != nullptr) {
            worker->Buffer.push_back(workItem);
        }
        if (worker->Buffer.size() < 2) {
            for (auto* w: worker->Buffer) {
                worker->LocalQueue.Enqueue(w);
            }
            worker->Buffer.clear();
            return;
        }
        TWorker* targetWorker = nullptr;
#ifndef _linux_
        TFMaybe<TGuard<TMutex>> wakeUpLockGuard = Nothing();
#endif
        for (auto& w: Workers) {
            ui64 dormant = 0;
#ifndef _linux_
            wakeUpLockGuard.ConstructInPlace(w.WakeUpLock);
#endif
            if (w.Ticket.compare_exchange_strong(dormant, worker->Buffer.size() / 2)) {
                targetWorker = &w;
                break;
            }
#ifndef _linux_
            wakeUpLockGuard.Clear();
#endif
        }
        if (!targetWorker) {
            for (auto* w: worker->Buffer) {
                worker->LocalQueue.Enqueue(w);
            }
            worker->Buffer.clear();
            return;
        }
        SortBy(worker->Buffer, [](auto* w) { return w->TaskHandle; });
        for (size_t i = 0; i < worker->Buffer.size(); i += 2) {
            worker->LocalQueue.Enqueue(worker->Buffer[i]);
        }
        for (size_t i = 1; i < worker->Buffer.size(); i += 2) {
            auto* item = worker->Buffer[i];
            item->LastWorker = targetWorker;
            targetWorker->ExternalQueue.Enqueue(item);
        }
        targetWorker->Wake();
        worker->Buffer.clear();
    }

    void TTaskExecutor::Start() {
        bool notStarted = false;
        Y_VERIFY(Started.compare_exchange_strong(notStarted, true), "already started");
        for (auto& w: Workers) {
            w.Executor = this;
            w.Thread = std::thread(&TTaskExecutor::Run, this, &w);
        }
    }

    void TTaskExecutor::Stop() {
        bool started = true;
        if (!Started.compare_exchange_strong(started, false)) {
            return;
        }
        for (auto& w : Workers) {
            w.EnqueueExternalAndWake(w.StopWorkItem);
        }
        for (auto& t : Workers) {
            t.Thread.join();
        }
        Workers.clear();
    }

    TIntrusivePtr<TTaskHandle> TTaskExecutor::Register(const TString& taskName,
        ETaskPriority taskPriority,
        const std::function<THolder<ITask>()>& createTask,
        ETaskActivationMode taskActivationMode)
    {
        auto result = CreateTaskHandle(taskName);
        result->CreateTask = createTask;
        const size_t tasksCount = taskActivationMode == ETaskActivationMode::Concurrent ? Workers.size() : 1;
        result->WorkItems.reserve(tasksCount);
        for (size_t i = 0; i < tasksCount; ++i) {
            result->WorkItems.push_back(MakeHolder<TTaskHandle::TWorkItem>(result.Get(), taskPriority, i));
        }
        return result;
    }

    TIntrusivePtr<TTaskHandle> TTaskExecutor::RegisterSingle(const TString& taskName,
        ETaskPriority taskPriority,
        ITask& task)
    {
        auto result = CreateTaskHandle(taskName);
        result->WorkItems.emplace_back(MakeHolder<TTaskHandle::TWorkItem>(result.Get(), taskPriority, 0));
        result->WorkItems.front()->SetTaskToExecute(task);
        return result;
    }

    TTaskHandle* TTaskExecutor::CurrentTask() {
        auto* currentWorker = TTaskExecutor::TWorker::Current;
        if (currentWorker == nullptr) {
            return nullptr;
        }
        Y_VERIFY(currentWorker->ExecutingItem != nullptr, "no executing item");
        return currentWorker->ExecutingItem->TaskHandle;
    }

    TTaskHandle& TTaskExecutor::CurrentTaskOrDie() {
        auto* result = CurrentTask();
        Y_VERIFY(result != nullptr, "no current task");
        return *result;
    }

    TIntrusivePtr<TTaskHandle> TTaskExecutor::CreateTaskHandle(const TString& taskName) {
        return MakeIntrusive<TTaskHandle>(taskName, Counters->GetSpecificTaskCounters(taskName), *this);
    }

    TSpecificTaskCounters::TSpecificTaskCounters()
        : Runs(*GetCounter("Runs", true))
        , RunDurationUs("RunDurationUs", *this)
    {
    }

    void TSpecificTaskCounters::Update() {
        RunDurationUs.Update();
    }

    TTaskExecutorCounters::TTaskExecutorCounters(const TIntrusivePtr<NMonitoring::TDynamicCounters>& counters)
        : TUpdatableCounters(counters)
        , SpecificTaskCounters()
        , Lock()
    {
    }

    TSpecificTaskCounters& TTaskExecutorCounters::GetSpecificTaskCounters(const TString& taskName) {
        with_lock(Lock) {
            if (auto it = SpecificTaskCounters.find(taskName); it != SpecificTaskCounters.end()) {
                return *it->second;
            }
            auto result = MakeIntrusive<TSpecificTaskCounters>();
            Unwrap()->RegisterSubgroup("task", taskName, result);
            Y_VERIFY(SpecificTaskCounters.insert({taskName, result.Get()}).second);
            return *result;
        }
    }

    void TTaskExecutorCounters::Update() {
        with_lock(Lock) {
            for (auto& t: SpecificTaskCounters) {
                t.second->Update();
            }
        }
    }
}
