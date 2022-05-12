#pragma once

#include "public.h"

#include <yt/yt/library/tracing/tracer.h>

#include <yt/yt/library/profiling/sensor.h>

#include <yt/yt/core/misc/mpsc_stack.h>
#include <yt/yt/core/misc/atomic_object.h>

#include <yt/yt/core/rpc/grpc/config.h>

#include <yt/yt/core/ytree/yson_serializable.h>

#include <library/cpp/yt/threading/spin_lock.h>

namespace NYT::NTracing {

////////////////////////////////////////////////////////////////////////////////

class TJaegerTracerDynamicConfig
    : public NYTree::TYsonSerializable
{
public:
    NRpc::NGrpc::TChannelConfigPtr CollectorChannelConfig;

    std::optional<i64> MaxRequestSize;

    std::optional<i64> MaxMemory;

    std::optional<double> SubsamplingRate;

    std::optional<TDuration> FlushPeriod;

    TJaegerTracerDynamicConfig();
};

DEFINE_REFCOUNTED_TYPE(TJaegerTracerDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

class TJaegerTracerConfig
    : public NYTree::TYsonSerializable
{
public:
    NRpc::NGrpc::TChannelConfigPtr CollectorChannelConfig;

    TDuration FlushPeriod;

    TDuration StopTimeout;

    TDuration RpcTimeout;

    TDuration QueueStallTimeout;

    TDuration ReconnectPeriod;

    i64 MaxRequestSize;

    i64 MaxBatchSize;

    i64 MaxMemory;

    std::optional<double> SubsamplingRate;

    // ServiceName is required by jaeger. When ServiceName is missing, tracer is disabled.
    std::optional<TString> ServiceName;

    THashMap<TString, TString> ProcessTags;

    bool EnablePidTag;

    TJaegerTracerConfig();

    TJaegerTracerConfigPtr ApplyDynamic(const TJaegerTracerDynamicConfigPtr& dynamicConfig);

    bool IsEnabled() const;
};

DEFINE_REFCOUNTED_TYPE(TJaegerTracerConfig)

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TJaegerTracer)

class TJaegerTracer
    : public ITracer
{
public:
    TJaegerTracer(
        const TJaegerTracerConfigPtr& config);

    TFuture<void> WaitFlush();

    void Configure(const TJaegerTracerConfigPtr& config);

    void Stop() override;

    void Enqueue(TTraceContextPtr trace) override;

private:
    const NConcurrency::TActionQueuePtr ActionQueue_;
    const NConcurrency::TPeriodicExecutorPtr FlushExecutor_;

    TAtomicObject<TJaegerTracerConfigPtr> Config_;

    TMpscStack<TTraceContextPtr> TraceQueue_;

    TInstant LastSuccessfullFlushTime_ = TInstant::Now();

    std::deque<std::pair<int, TSharedRef>> BatchQueue_;
    i64 QueueMemory_ = 0;
    i64 QueueSize_ = 0;

    TAtomicObject<TPromise<void>> QueueEmptyPromise_ = NewPromise<void>();

    NRpc::IChannelPtr CollectorChannel_;
    NRpc::NGrpc::TChannelConfigPtr OpenChannelConfig_;
    TInstant ChannelReopenTime_;

    void Flush();
    void DequeueAll(const TJaegerTracerConfigPtr& config);
    void NotifyEmptyQueue();

    std::tuple<std::vector<TSharedRef>, int, int> PeekQueue(const TJaegerTracerConfigPtr& config);
    void DropQueue(int i);

    NProfiling::TCounter TracesDequeued_;
    NProfiling::TCounter TracesDropped_;
    NProfiling::TCounter PushedBytes_;
    NProfiling::TCounter PushErrors_;
    NProfiling::TSummary PayloadSize_;
    NProfiling::TGauge MemoryUsage_;
    NProfiling::TGauge TraceQueueSize_;
    NProfiling::TEventTimer PushDuration_;

    TSharedRef GetProcessInfo(const TJaegerTracerConfigPtr& config);
};

DEFINE_REFCOUNTED_TYPE(TJaegerTracer)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTracing
