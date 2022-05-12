#pragma once

#include <logbroker/unified_agent/common/common.h>
#include <logbroker/unified_agent/common/delayed_executor.h>
#include <logbroker/unified_agent/common/tasks.h>
#include <logbroker/unified_agent/common/util/clock.h>

#include <library/cpp/threading/future/future.h>

#include <variant>

namespace NUnifiedAgent {
    template <typename THandler, typename... TArgs>
    class TSequentialExecutor: private ITask {
    public:
        TSequentialExecutor(THandler& handler, TTaskExecutor& taskExecutor,
            TDelayedExecutor& delayedExecutor, const TString& name, ETaskPriority taskPriority)
            : TaskExecutor(taskExecutor)
            , Name(name)
            , TaskPriority(taskPriority)
            , TaskHandle_(nullptr)
            , Queue()
            , ScheduledEvents()
            , Lock()
            , LocalTimersQueue([this]() { Post(TTimerEvent{}); }, Name + "_timer", delayedExecutor)
            , Visitor(handler, *this) {
        }

        void Register() {
            TaskHandle_ = TaskExecutor.RegisterSingle(Name, TaskPriority, *this);
        }

        NThreading::TFuture<void> Unregister() {
            return TaskHandle_->Unregister();
        }

        inline TTaskHandle& TaskHandle() noexcept {
            return *TaskHandle_;
        }

        template <typename TArg>
        void Post(TArg&& v, bool wakeUp = false) {
            bool wasEmpty;
            with_lock(Lock) {
                wasEmpty = Queue.Empty();
                Queue.Enqueue(std::move(v));
            }
            if (wasEmpty) {
                TaskHandle_->Pulse(wakeUp);
            }
        }

        template <typename TArg>
        bool SchedulePost(TArg&& v, TDuration timeout) {
            if (LocalTimersQueue.IsFinalized()) {
                return false;
            }
            THolder<TScheduledEvent>& newEvent =
                ScheduledEvents.emplace_back(MakeHolder<TScheduledEvent>(std::move(v)));
            newEvent->Deadline.Data() = ScheduledEvents.size() - 1;
            LocalTimersQueue.SetTimer(newEvent->Deadline, TClock::Now() + timeout);
            return true;
        }

        void FinalizeScheduledEventsQueue() {
            LocalTimersQueue.Finalize();
        }

        bool Registered() {
            return TaskHandle_ != nullptr;
        }

    private:
        void SendReadyEvents() {
            const auto now = TClock::Now();
            while (!LocalTimersQueue.Empty()) {
                TLocalTimersQueue::TDeadline& deadline = LocalTimersQueue.Top();
                if (deadline.Value() > now) {
                    break;
                }
                size_t scheduledEventIdx = deadline.Data();
                LocalTimersQueue.ResetTimer(deadline);
                Post(std::move(
                    ScheduledEvents[scheduledEventIdx]->Event)); // finally, post scheduled event
                if (scheduledEventIdx != ScheduledEvents.size() - 1) {
                    ScheduledEvents.back()->Deadline.Data() = scheduledEventIdx;
                    ScheduledEvents[scheduledEventIdx] = std::move(ScheduledEvents.back());
                }
                ScheduledEvents.pop_back();
            }
        }

        bool Run() override {
            TVector<TArg>* args;
            with_lock(Lock) {
                args = Queue.Dequeue();
            }
            if (args) {
                for (auto& e: *args) {
                    std::visit(Visitor, e);
                }
            }
            return false;
        }

    private:
        struct TTimerEvent {};

        using TArg = std::variant<TTimerEvent, TArgs...>;

        struct TScheduledEvent {
            explicit TScheduledEvent(TArg&& event)
                : Event(std::move(event))
                , Deadline() {
            }

            TArg Event;
            TLocalTimersQueue::TDeadline Deadline;
        };

        struct TVisitor {
            explicit TVisitor(THandler& handler, TSequentialExecutor<THandler, TArgs...>& owner)
                : Handler(handler)
                , Owner(owner) {
            }

            inline void operator()(TTimerEvent& /*e*/) noexcept {
                Owner.SendReadyEvents();
            }

            template <typename T>
            inline void operator()(T& e) noexcept {
                Handler.Handle(e);
            }

            THandler& Handler;
            TSequentialExecutor<THandler, TArgs...>& Owner;
        };

    private:
        TTaskExecutor& TaskExecutor;
        TString Name;
        ETaskPriority TaskPriority;
        TIntrusivePtr<TTaskHandle> TaskHandle_;
        TSwapQueue<TArg> Queue;
        TVector<THolder<TScheduledEvent>> ScheduledEvents;
        TAdaptiveLock Lock;
        TLocalTimersQueue LocalTimersQueue;
        TVisitor Visitor;
    };
}
