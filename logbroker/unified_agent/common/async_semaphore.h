#pragma once

#include <library/cpp/threading/future/future.h>

#include <util/generic/deque.h>

namespace NUnifiedAgent {
    class TAsyncSemaphore {
    public:
        explicit TAsyncSemaphore(size_t maxPermits);

        NThreading::TFuture<void> Acquire();

        void Release();

        void Cancel();

        size_t GetAcquiredPermits() const;

    private:
        size_t MaxPermits;
        size_t AcquiredPermits;
        TDeque<NThreading::TPromise<void>> Promises;
        bool Cancelled;
        TAdaptiveLock Lock;
    };
}
