#include "future.h"

using namespace NThreading;

namespace NUnifiedAgent {
    TFuture<void> SwallowException(const TFuture<void>& future) noexcept {
        auto p = NewPromise();
        future.Subscribe([p](const auto&) mutable {
            p.SetValue();
        });
        return p.GetFuture();
    }

    TSerializingInvoker::TSerializingInvoker()
        : HasPendingInvocation_(false)
        , Queue_()
        , Lock_()
    {
    }

    TFuture<void> TSerializingInvoker::Invoke(TFunc&& func) {
        with_lock(Lock_) {
            if (HasPendingInvocation_) {
                Queue_.push_back({std::move(func), NewPromise()});
                return Queue_.back().Promise.GetFuture();
            }
            HasPendingInvocation_ = true;
        }
        return DoInvoke(std::move(func), TPromise<void>());
    }

    TFuture<void> TSerializingInvoker::DoInvoke(TFunc&& func, TPromise<void>&& promise) {
        auto future = func();
        future.Subscribe([this, p = std::move(promise)](const auto& f) mutable {
            VerifyHasValue(f);

            if (p.Initialized()) {
                p.SetValue();
            }

            TQueueItem queueItem;
            with_lock(Lock_) {
                if (Queue_.empty()) {
                    HasPendingInvocation_ = false;
                    return;
                }
                queueItem = std::move(Queue_.front());
                Queue_.pop_front();
            }

            DoInvoke(std::move(queueItem.Func), std::move(queueItem.Promise));
        });
        return future;
    }
}
