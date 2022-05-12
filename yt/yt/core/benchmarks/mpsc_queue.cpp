#include <yt/yt/core/misc/mpsc_stack.h>
#include <yt/yt/core/misc/mpsc_queue.h>
#include <yt/yt/core/misc/relaxed_mpsc_queue.h>

#include <yt/yt/core/concurrency/moody_camel_concurrent_queue.h>

#include <benchmark/benchmark.h>

#include <util/thread/lfqueue.h>

#include <thread>

namespace NYT::NConcurrency {
namespace {

////////////////////////////////////////////////////////////////////////////////

template <typename TQueueImpl>
class TMpscBenchmark
    : public TQueueImpl
    , public benchmark::Fixture
{
public:
    void Execute(benchmark::State& state)
    {
        if (state.thread_index != 0) {
            while (state.KeepRunning()) {
                TQueueImpl::Produce();
            }
        } else {
            while (state.KeepRunning()) {
                TQueueImpl::Consume();
            }
        }
    }
};

template <typename TQueueImpl>
class TMpscTokenBenchmark
    : public TQueueImpl
    , public benchmark::Fixture
{
public:
    void Execute(benchmark::State& state)
    {
        if (state.thread_index != 0) {
            while (state.KeepRunning()) {
                TQueueImpl::Produce();
            }
        } else {
            auto token = TQueueImpl::MakeConsumerToken();
            while (state.KeepRunning()) {
                TQueueImpl::Consume(&token);
            }
        }
    }
};

#define DECLARE_MT_BENCHMARK(TBenchmark, Impl) \
    BENCHMARK_DEFINE_F(TBenchmark, Impl)(benchmark::State& state) { Execute(state); } \
    BENCHMARK_REGISTER_F(TBenchmark, Impl)->ThreadRange(1 << 1, 1 << 5)->UseRealTime();

class TRelaxedMpscQueueImpl
{
public:
    void Produce()
    {
        Queue_.Enqueue(3);
    }

    bool Consume()
    {
        int v;
        return Queue_.TryDequeue(&v);
    }

private:
    TRelaxedMpscQueue<int> Queue_;
};

class TMpscQueueImpl
{
public:
    void Produce()
    {
        Queue_.Enqueue(3);
    }

    bool Consume()
    {
        int v;
        return Queue_.TryDequeue(&v);
    }

private:
    TMpscQueue<int> Queue_;
};

class TMpscStackImpl
{
public:
    void Produce()
    {
        Stack_.Enqueue(3);
    }

    bool Consume()
    {
        return Stack_.DequeueAll(false, [] (const auto&) { });
    }

private:
    TMpscStack<int> Stack_;
};

class TLFQueueImpl
{
public:
    void Produce()
    {
        Queue_.Enqueue(3);
    }

    bool Consume()
    {
        std::vector<int> values;
        Queue_.DequeueAll(&values);
        return !values.empty();
    }

private:
    TLockFreeQueue<int> Queue_;
};

class TMoodyCamelQueueImpl
{
public:
    void Produce()
    {
        Queue_.enqueue(3);
    }

    bool Consume(moodycamel::ConsumerToken* token = nullptr)
    {
        int v;
        if (token) {
            return Queue_.try_dequeue(*token, v);
        } else {
            return Queue_.try_dequeue(v);
        }
    }

    moodycamel::ConsumerToken MakeConsumerToken()
    {
        return moodycamel::ConsumerToken(Queue_);
    }

private:
    moodycamel::ConcurrentQueue<int> Queue_;
};

using TRelaxedMpscQueueBenchmark = TMpscBenchmark<TRelaxedMpscQueueImpl>;
DECLARE_MT_BENCHMARK(TRelaxedMpscQueueBenchmark, Mpsc)

using TMpscQueueBenchmark = TMpscBenchmark<TMpscQueueImpl>;
DECLARE_MT_BENCHMARK(TMpscQueueBenchmark, Mpsc)

using TMpscStackBenchmark = TMpscBenchmark<TMpscStackImpl>;
DECLARE_MT_BENCHMARK(TMpscStackBenchmark, Mpsc)

using TLFQueueBenchmark = TMpscBenchmark<TLFQueueImpl>;
DECLARE_MT_BENCHMARK(TLFQueueBenchmark, Mpsc)

using TMpscMoodyCamelQueueBenchmark = TMpscBenchmark<TMoodyCamelQueueImpl>;
DECLARE_MT_BENCHMARK(TMpscMoodyCamelQueueBenchmark, Mpsc)

using TMpscMoodyCamelQueueTokenBenchmark = TMpscTokenBenchmark<TMoodyCamelQueueImpl>;
DECLARE_MT_BENCHMARK(TMpscMoodyCamelQueueTokenBenchmark, Mpsc)

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NConcurrency
