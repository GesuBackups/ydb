#pragma once

#include <logbroker/unified_agent/common/util/logger.h>

#include <util/thread/pool.h>

namespace NUnifiedAgent {
    class TNamedThreadPool: public IThreadPool {
    public:
        TNamedThreadPool(const TString& threadName, TScopeLogger& logger);

        ~TNamedThreadPool();

        bool Add(IObjectInQueue* obj) override;

        void Start(size_t threadCount, size_t queueSizeLimit) override;

        void Stop() noexcept override;

        size_t Size() const noexcept override;

    private:
        class TFactory;

    private:
        THolder<TFactory> Factory;
        TThreadPool ThreadPool;
        std::atomic<ui64> StartCount;
        TScopeLogger& Logger;
    };
}
