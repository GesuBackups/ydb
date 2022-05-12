#include "async_semaphore.h"

using namespace NThreading;

namespace NUnifiedAgent {
    TAsyncSemaphore::TAsyncSemaphore(size_t maxPermits)
        : MaxPermits(maxPermits)
        , AcquiredPermits(0)
        , Promises()
        , Cancelled(false)
        , Lock()
    {
        Y_VERIFY(MaxPermits > 0);
    }

    TFuture<void> TAsyncSemaphore::Acquire() {
        with_lock(Lock) {
            if (Cancelled || AcquiredPermits < MaxPermits) {
                ++AcquiredPermits;
                return MakeFuture();
            }
            auto p = NewPromise();
            Promises.push_back(p);
            return p.GetFuture();
        }
    }

    void TAsyncSemaphore::Release() {
        TPromise<void> p;
        with_lock(Lock) {
            Y_VERIFY(AcquiredPermits > 0);
            if (Cancelled || Promises.empty()) {
                --AcquiredPermits;
                return;
            }
            p = Promises.front();
            Promises.pop_front();
        }
        p.SetValue();
    }

    void TAsyncSemaphore::Cancel() {
        TDeque<TPromise<void>> promises{};
        with_lock(Lock) {
            Y_VERIFY(!Cancelled);
            Cancelled = true;
            AcquiredPermits += Promises.size();
            DoSwap(promises, Promises);
        }
        for (auto& p: promises) {
            p.SetValue();
        }
    }

    size_t TAsyncSemaphore::GetAcquiredPermits() const {
        with_lock(Lock) {
            return AcquiredPermits;
        }
    }
}
