#pragma once

#include <logbroker/unified_agent/common/backoff.h>
#include <logbroker/unified_agent/common/delayed_executor.h>
#include <logbroker/unified_agent/common/tasks.h>

#include <util/random/fast.h>
#include <util/system/filemap.h>

namespace NUnifiedAgent {
    class IJitter {
    public:
        virtual ~IJitter() = default;

        virtual TDuration Apply(TDuration delay) = 0;
    };

    class THalfJitter: public IJitter {
    public:
        THalfJitter();

        THalfJitter(ui64 seed);

        TDuration Apply(TDuration delay) override;

    private:
        TFastRng<ui32> Rng_;
    };

    class TSymJitter: public IJitter {
    public:
        explicit TSymJitter(TDuration max);

        TDuration Apply(TDuration period) override;

    private:
        const TDuration Max;
    };

    using TPollCallback = std::function<NThreading::TFuture<void>(TInstant)>;

    class TPollingParameters {
    public:
        TPollingParameters(TDuration period, const TString& name)
            : Period_(period)
            , Name_(name)
            , AlignToGrid_(false)
            , Jitter_(nullptr)
            , ExceptionsBackoff_(nullptr)
            , TaskPriority_(ETaskPriority::Normal)
        {
        }

        TPollingParameters&& AlignToGrid(bool value = true) && {
            AlignToGrid_ = value;
            return std::move(*this);
        }

        TPollingParameters&& WithJitter(THolder<IJitter>&& jitter) && {
            Jitter_ = std::move(jitter);
            return std::move(*this);
        }

        TPollingParameters&& RetryExceptions(THolder<IBackoff> exceptionsBackoff) && {
            ExceptionsBackoff_ = std::move(exceptionsBackoff);
            return std::move(*this);
        }

        TPollingParameters&& WithTaskPriority(ETaskPriority taskPriority) && {
            TaskPriority_ = taskPriority;
            return std::move(*this);
        }

        friend class TPeriodicExecutor;

    private:
        const TDuration Period_;
        const TString Name_;
        bool AlignToGrid_;
        THolder<IJitter> Jitter_;
        THolder<IBackoff> ExceptionsBackoff_;
        ETaskPriority TaskPriority_;
    };

    class TPeriodicExecutor: private ITask {
    public:
        TPeriodicExecutor(TPollCallback&& callback,
                          TPollingParameters&& parameters,
                          TDelayedExecutor& delayedExecutor,
                          TTaskExecutor& taskExecutor,
                          TScopeLogger& logger);

        ~TPeriodicExecutor() override;

        void Start();

        NThreading::TFuture<void> Stop();

        void TriggerOutOfBand(TInstant triggerTime);

        void SetPeriod(TDuration period);

    private:
        bool Run() override;

        TInstant Schedule(TDuration delay);

    private:
        TPollCallback Callback;
        const TPollingParameters Parameters;
        TDelayedExecutor& DelayedExecutor;
        TTaskExecutor& TaskExecutor;
        TIntrusivePtr<TTaskHandle> RegisteredTask;
        NThreading::TFuture<void> PendingExecution;
        bool Started;
        bool Executing;
        TFMaybe<TInstant> TriggerTime;
        TDuration Period;
        TAdaptiveLock Lock;
        TTimer Timer;
        TScopeLogger Logger;
    };
}
