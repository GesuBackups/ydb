#pragma once

#include <util/generic/ptr.h>
#include <util/system/spinlock.h>

namespace NUnifiedAgent {
    template <typename T>
    class TSimplePool {
    public:
        struct TScope {
            explicit TScope(TSimplePool& pool)
                : Pool(pool)
                , Value(Pool.Acquire())
            {
            }

            ~TScope() {
                Pool.Release(Value);
            }

            TSimplePool& Pool;

            T& Value;
        };

    public:
        TSimplePool(std::function<THolder<T>()> createItem, std::function<void(T&)> resetItem = nullptr)
            : CreateItem(std::move(createItem))
            , ResetItem(std::move(resetItem))
            , CreatedItems()
            , AvailableItems()
            , Lock()
        {
        }

        T& Acquire() {
            with_lock(Lock) {
                if (AvailableItems.empty()) {
                    CreatedItems.push_back(CreateItem());
                    AvailableItems.push_back(CreatedItems.back().Get());
                }
                auto result = AvailableItems.back();
                AvailableItems.pop_back();
                return *result;
            }
        }

        void Release(T& item) {
            if (ResetItem) {
                ResetItem(item);
            }
            with_lock(Lock) {
                AvailableItems.push_back(&item);
            }
        }

        template <typename TContainer>
        void Release(const TContainer& container) {
            with_lock(Lock) {
                for (auto* item: container) {
                    AvailableItems.push_back(item);
                }
            }
        }

        TScope Scope() {
            return TScope(*this);
        }

    private:
        std::function<THolder<T>()> CreateItem;
        std::function<void(T&)> ResetItem;
        TVector<THolder<T>> CreatedItems;
        TVector<T*> AvailableItems;
        TAdaptiveLock Lock;
    };
}
