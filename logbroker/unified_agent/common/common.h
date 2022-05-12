#pragma once

#include <logbroker/unified_agent/common/util/f_maybe.h>
#include <logbroker/unified_agent/common/util/logger.h>

#include <util/datetime/base.h>
#include <util/generic/deque.h>
#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/random/random.h>
#include <util/string/builder.h>
#include <util/string/join.h>
#include <util/system/mutex.h>
#include <util/system/spinlock.h>
#include <util/system/type_name.h>

namespace NUnifiedAgent {
    template <typename T>
    void DequeueMany(TVector<T>& target, TDeque<T>& source, size_t count) {
        if (source.empty() || count == 0) {
            return;
        }
        auto begin = source.begin();
        auto end = std::next(begin, Min(count, source.size()));
        target.resize(static_cast<size_t>(end - begin));
        std::move(begin, end, target.begin());
        source.erase(begin, end);
    }

    template <typename T>
    void EnqueueMany(TDeque<T>& target, TArrayRef<T> source) {
        if (source.empty()) {
            return;
        }
        target.resize(target.size() + source.size());
        std::move(source.begin(), source.end(), std::prev(target.end(), source.size()));
    }

    template <typename TTarget, typename TSelf>
    bool Is(TSelf& self) {
        return dynamic_cast<TTarget*>(&self) != nullptr;
    }

    TString ExpandHomePrefix(const TString& s);

    TString CamelCaseToSnakeCase(const TStringBuf& str);

    template <typename T, typename TFunc>
    auto InvokeNoExcept(T* instance, TFunc invoker, const char* desc) -> decltype(invoker(*instance)) {
        try {
            return invoker(*instance);
        } catch(...) {
            Y_FAIL("%s exception, instance [%s], error [%s]",
                   desc, TypeName(*instance).c_str(), CurrentExceptionMessage().c_str());
        }
    }

    template <typename TContainer, typename F>
    TString JoinSeq(const TStringBuf delimiter, const TContainer& container, F&& f) {
        using std::distance;

        TVector<TString> items;
        const auto b = container.begin();
        const auto e = container.end();
        items.reserve(distance(b, e));
        for (auto it = b; it != e; ++it) {
            items.push_back(ToString(f(*it)));
        }
        return JoinSeq(delimiter, items);
    }

    template <typename TContainer>
    auto MinOf(const TContainer& container) {
        using std::begin;
        using std::end;
        return *MinElement(begin(container), end(container));
    }

    template <typename TTarget, typename TSource>
    void Append(TTarget& target, const TSource& source) {
        using std::begin;
        using std::end;
        target.insert(end(target), begin(source), end(source));
    }

    template <typename T>
    struct TIntrusiveHolder final: public TAtomicRefCount<TIntrusiveHolder<T>> {
        TIntrusiveHolder() = default;

        explicit TIntrusiveHolder(T&& value)
            : Value(std::move(value))
        {
        }

        T Value;
    };

    template <typename T>
    class TSwapQueue {
    public:
        TSwapQueue()
            : WriteQueue()
            , ReadQueue()
        {
        }

        inline bool Empty() const noexcept {
            return WriteQueue.empty();
        }

        inline void Enqueue(T&& value) noexcept {
            WriteQueue.push_back(std::move(value));
        }

        TVector<T>* Dequeue() {
            if (WriteQueue.empty()) {
                return nullptr;
            }
            ReadQueue.clear();
            DoSwap(WriteQueue, ReadQueue);
            return &ReadQueue;
        }

        void Clear() {
            WriteQueue.clear();
        }

    private:
        TVector<T> WriteQueue;
        TVector<T> ReadQueue;
    };

    template <typename TKey, typename TValue>
    TString DescribeHashMap(const THashMap<TKey, TValue>& hashMap) {
        if (hashMap.empty()) {
            return "";
        }
        TVector<TKey> sortedKeys;
        sortedKeys.reserve(hashMap.size());
        for (const auto& s: hashMap) {
            sortedKeys.push_back(s.first);
        }
        Sort(sortedKeys);
        TStringBuilder builder;
        for (const auto& k: sortedKeys) {
            builder << k << '=' << hashMap.at(k) << ',';
        }
        return std::move(builder.remove(builder.size() - 1));
    }

    template <typename TKey, typename TValue>
    THashMap<TKey, TValue>&& MergeHashMap(THashMap<TKey, TValue>&& a, const THashMap<TKey, TValue>& b) {
        for (const auto& p: b) {
            a[p.first] = p.second;
        }
        return std::move(a);
    }

    template <template<typename...> class TTargetContainer, typename TSourceContainer>
    inline auto CopyAs(const TSourceContainer& source) {
        using std::begin;
        using std::end;
        return TTargetContainer<typename TSourceContainer::value_type>(begin(source), end(source));
    }

    // preserves ordering for the left part
    // std::stable_partition is usually an overkill, it is allowed to allocate for example
    // copy-pasted as is from std::partition overload for forward iterator
    template <typename TPredicate, typename TIt>
    TIt SemiStablePartition(TIt first, TIt last, TPredicate predicate) {
        while (true) {
            if (first == last) {
                return first;
            }
            if (!predicate(*first)) {
                break;
            }
            ++first;
        }
        for (TIt p = first; ++p != last; ) {
            if (predicate(*p)) {
                DoSwap(*first, *p);
                ++first;
            }
        }
        return first;
    }

    template <typename TPredicate, typename TContainer>
    auto SemiStablePartition(TContainer& c, TPredicate predicate) {
        using std::begin;
        using std::end;
        return SemiStablePartition(begin(c), end(c), predicate);
    }

    template <typename T>
    inline T RandomNumberInRange(T min, T max) {
        Y_VERIFY(max > min);
        return RandomNumber<ui64>(max - min) + min;
    }

    inline TInstant RandomInstantInRange(TInstant min, TInstant max) {
        return TInstant::FromValue(RandomNumberInRange(min.GetValue(), max.GetValue()));
    };

    template <typename TContainer>
    ui64 IterToIndex(const TContainer& container, typename TContainer::const_reverse_iterator it) {
        const auto result = std::distance(begin(container), std::prev(it.base()));
        return static_cast<ui64>(result);
    }

    template <typename TTag>
    class TTypedMutex: public TMutex {
    public:
        using TLockGuard = TGuard<TTypedMutex>;
    };

    template <typename TTag>
    class TTypedAdaptiveLock: public TAdaptiveLock {
    public:
        using TLockGuard = TGuard<TTypedAdaptiveLock>;
    };

    struct TCopyMarker {};

    enum class EHostNameStyle {
        Long /* "host_name" */,
        Short /* "short_host_name" */
    };

    TString GetHostName(EHostNameStyle style);

    TString CaptureBacktrace();

    THashMap<TString, TString> MergeCurrentEnv(THashMap<TString, TString>&& env);

    bool TryParseDataSize(TStringBuf s, size_t& result);

    template <typename T>
    inline TString FormatTypeName() {
        if constexpr (std::is_same_v<T, TString>) {
            return "string";
        }
        if constexpr (std::is_same_v<T, size_t>) {
            return "unsigned long";
        }
        if constexpr (std::is_same_v<T, i64>) {
            return "long";
        }
        if constexpr (std::is_enum_v<T>) {
            auto typeName = TypeName<T>();
            SubstGlobal(typeName, "enum ", "");
            return typeName;
        }
        if constexpr (std::is_class_v<T>) {
            auto typeName = TypeName<T>();
            SubstGlobal(typeName, "class ", "");
            return typeName;
        }
        return TypeName<T>();
    }

    void ReportListenedPorts(TScopeLogger& logger);

    TString ShortExceptionMessage();
}
