#pragma once

#include <logbroker/unified_agent/common/common.h>
#include <logbroker/unified_agent/common/error.h>

#include <library/cpp/threading/cancellation/operation_cancelled_exception.h>
#include <library/cpp/threading/future/future.h>

#include <util/generic/vector.h>
#include <util/generic/deque.h>

#include <atomic>

namespace NUnifiedAgent {
    namespace NImpl {
        template <typename T>
        struct TWaitAll final: public TAtomicRefCount<TWaitAll<T>> {
            TVector<NThreading::TFuture<T>> Futures;
            std::atomic<int> UnfinishedFutures;
            NThreading::TPromise<TVector<T>> Promise;

            explicit TWaitAll(TVector<NThreading::TFuture<T>>&& futures)
                : Futures(std::move(futures))
                , UnfinishedFutures(static_cast<int>(Futures.size()))
                , Promise(NThreading::NewPromise<TVector<T>>())
            {
            }

            void Set() {
                const auto prev = UnfinishedFutures.fetch_sub(1);
                Y_VERIFY(prev > 0, "%d", prev);
                if (prev == 1) {
                    try {
                        TVector<T> result;
                        result.reserve(Futures.size());
                        for (auto& v : Futures) {
                            Y_VERIFY(v.HasValue() || v.HasException());
                            result.push_back(v.ExtractValueSync());
                        }
                        Promise.SetValue(std::move(result));
                    } catch (...) {
                        Y_VERIFY(!Promise.HasValue());
                        Promise.TrySetException(std::current_exception());
                    }
                }
            }
        };

        template <typename T>
        struct TFuncResultTraits {
            using TType = T;

            static constexpr auto IsFuture = false;
        };

        template <typename T>
        struct TFuncResultTraits<NThreading::TFuture<T>> {
            using TType = typename TFuncResultTraits<T>::TType;

            static constexpr auto IsFuture = true;
        };

        template <typename TFunc>
        using TFuncTraits = TFuncResultTraits<decltype(std::declval<TFunc>()())>;

        template <typename TFunc>
        using TFuncFutureType = typename TFuncTraits<TFunc>::TType;
    }

    template <typename T>
    inline auto WaitAll(TVector<NThreading::TFuture<T>>&& futures) {
        auto waiter = MakeIntrusive<NImpl::TWaitAll<T>>(std::move(futures));
        for (auto& f: waiter->Futures) {
            f.Subscribe([=](const auto&) {
                waiter->Set();
            });
        }
        return waiter->Promise.GetFuture();
    }

    template <typename T>
    inline TErrorOr<T> ExtractValue(const NThreading::TFuture<T>& future) noexcept {
        try {
            return future.GetValue();
        } catch(...) {
            return TError{ShortExceptionMessage()};
        }
    }

    inline TErrorOrVoid ExtractValue(const NThreading::TFuture<void>& future) noexcept {
        try {
            future.GetValue();
            return Void();
        } catch(...) {
            return TError{ShortExceptionMessage()};
        }
    }

    template <typename T>
    inline void VerifyHasValue(const NThreading::TFuture<T>& future) {
        if (future.HasValue()) {
            return;
        }
        if (future.HasException()) {
            try {
                future.TryRethrow();
            } catch (...) {
                Y_FAIL("unexpected future exception: [%s]", CurrentExceptionMessage().c_str());
            }
            Y_FAIL("no exception");
        }
        Y_FAIL("unexpected future state");
    }

    NThreading::TFuture<void> SwallowException(const NThreading::TFuture<void>& future) noexcept;

    template <typename TInvoker, typename... T>
    NThreading::TFuture<void> CombineSequentially(TInvoker&& invoker, T&... args) {
        NThreading::TFuture<void> result = NThreading::MakeFuture();
        ApplyToMany([&](auto&& v) {
            if (v) {
                result = result.Apply([&](const auto& f) {
                    f.TryRethrow();
                    return invoker(*v);
                });
            }
        }, args...);
        return result;
    }

    template <typename TInvoker, typename TContainer>
    NThreading::TFuture<void> CombineInParallel(TContainer& container, TInvoker&& invoker) {
        auto result = TVector<NThreading::TFuture<void>>(Reserve(container.size()));
        for (auto& v: container) {
            result.push_back(invoker(v));
        }
        return WaitAll(result);
    }

    template <typename... T>
    NThreading::TFuture<void> StartSequentially(T&... args) {
        return CombineSequentially([](auto& v) { return v.Start(); }, args...);
    }

    template <typename... T>
    NThreading::TFuture<void> StopSequentially(T&... args) {
        return CombineSequentially([](auto& v) { return v.Stop(); }, args...);
    }

    class TSerializingInvoker {
    public:
        using TFunc = std::function<NThreading::TFuture<void>()>;

    public:
        TSerializingInvoker();

        NThreading::TFuture<void> Invoke(TFunc&& func);

    private:
        NThreading::TFuture<void> DoInvoke(TFunc&& func, NThreading::TPromise<void>&& promise);

    private:
        struct TQueueItem {
            TFunc Func;
            NThreading::TPromise<void> Promise;
        };

    private:
        bool HasPendingInvocation_;
        TDeque<TQueueItem> Queue_;
        TAdaptiveLock Lock_;
    };

    template <typename TFunc>
    NThreading::TFuture<NImpl::TFuncFutureType<TFunc>> FuturizeInvoke(TFunc&& func) {
        try {
            if constexpr (NImpl::TFuncTraits<TFunc>::IsFuture) {
                return func();
            } else {
                return NThreading::MakeFuture(func());
            }
        } catch (...) {
            return NThreading::MakeErrorFuture<NImpl::TFuncFutureType<TFunc>>(std::current_exception());
        }
    }

    template <typename T>
    using TAsyncFactory = std::function<NThreading::TFuture<T>(const NThreading::TFuture<void>&)>;

    template <typename T, typename TFunc>
    NThreading::TFuture<NThreading::TFutureType<NThreading::TFutureCallResult<TFunc, T>>> ApplyWithCancellation(
        const NThreading::TFuture<T>& future,
        const NThreading::TFuture<void>& cancel,
        TFunc&& func)
    {
        return NThreading::WaitAny(cancel, future.IgnoreResult())
            .Apply([cancel, future, func = std::move(func)](const auto&) {
                Y_VERIFY(!cancel.HasException());
                if (cancel.HasValue()) {
                    throw NThreading::TOperationCancelledException();
                }
                Y_VERIFY(future.HasValue() || future.HasException());
                return func(future);
            });
    }
}
