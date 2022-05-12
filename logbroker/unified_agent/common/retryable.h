#pragma once
#include <logbroker/unified_agent/common/backoff.h>
#include <logbroker/unified_agent/common/delayed_executor.h>
#include <logbroker/unified_agent/common/sequential_executor.h>
#include <logbroker/unified_agent/common/tasks.h>
#include <library/cpp/threading/cancellation/operation_cancelled_exception.h>

namespace NUnifiedAgent {
    template <typename T, typename Err>
    class TRetryable {
    public:
        TRetryable(std::function<T()>&& factory,
            THolder<IBackoff>&& backoff,
            TDelayedExecutor& delayedExecutor,
            TTaskExecutor& executor,
            TScopeLogger& logger,
            const TString& name)
            : Factory(factory)
            , Backoff(std::move(backoff))
            , DelayedExecutor(delayedExecutor)
            , Events(*this, executor, delayedExecutor, name, ETaskPriority::Normal)
            , Timer([this]() { Events.Post(TRetryEvent{}); }, name)
            , Logger(logger.Child(name))
            , ResultPromise(NThreading::NewPromise<T>())
            , Name(name)
        {
            Y_VERIFY(Backoff);
        }

        void Start() {
            MakeAttempt(true);
        }

        NThreading::TFuture<void> Stop() {
            if (Events.Registered()) {
                Events.Post(TStopEvent{});
                return ResultPromise.GetFuture().Apply([this](const auto& /* f */){
                    return Events.Unregister();
                });
            }
            return NThreading::MakeFuture();
        }

        NThreading::TFuture<T> Result() {
            return ResultPromise.GetFuture();
        }

    public:
        struct TStopEvent {};

        struct TRetryEvent {};

        using TEvents = TSequentialExecutor<TRetryable<T, Err>, TStopEvent, TRetryEvent>;

    public:
        void Handle(TStopEvent&) {
            DelayedExecutor.ResetTimer(Timer);  // try reset the last timer if existent
            ResultPromise.TrySetException(std::make_exception_ptr(NThreading::TOperationCancelledException()));
        }

        void Handle(TRetryEvent&) {
            if (ResultPromise.HasException()) {
                YLOG_INFO("cancelled");
                return;
            }

            MakeAttempt(false);
        }

    private:
        void MakeAttempt(bool isFirst) {
            Y_VERIFY(!ResultPromise.HasValue());
            const auto* desc = isFirst ? "first attempt" : "attempt";
            try {
                YLOG_DEBUG(Sprintf("%s started", desc));
                auto v = Factory();
                YLOG_DEBUG(Sprintf("%s succeeded", desc));
                try {
                    ResultPromise.SetValue(std::move(v));
                } catch (...) {
                    Y_FAIL("must not throw");
                }
            } catch (const Err& err) {
                const auto retryAt = TClock::Now() + Backoff->Next();
                YLOG_WARNING(Sprintf("%s failed, error [%s], will retry at [%s]",
                    desc, err.what(), ToString(retryAt).c_str()));
                if (isFirst) {
                    Events.Register();
                }
                DelayedExecutor.SetTimer(Timer, retryAt);
            } catch (...) {
                YLOG_DEBUG(Sprintf("%s failed, unexpected exception [%s]",
                    desc, CurrentExceptionMessage().c_str()));
                if (isFirst) {
                    throw;
                }
                ResultPromise.SetException(std::current_exception());
            }
        }

    private:
        std::function<T()> Factory;
        THolder<IBackoff> Backoff;
        TDelayedExecutor& DelayedExecutor;
        TEvents Events;
        TTimer Timer;
        TScopeLogger Logger;
        NThreading::TPromise<T> ResultPromise;
        bool Stopped;
        TString Name;
    };
}
