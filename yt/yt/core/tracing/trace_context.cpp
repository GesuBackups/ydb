#include "trace_context.h"
#include "private.h"
#include "config.h"
#include "allocation_tags.h"

#include <yt/yt/core/profiling/timing.h>

#include <yt/yt/core/misc/atomic_object.h>
#include <yt/yt/core/misc/protobuf_helpers.h>
#include <yt/yt/core/misc/singleton.h>

#include <yt/yt/core/ytree/convert.h>

#include <yt/yt_proto/yt/core/tracing/proto/tracing_ext.pb.h>

#include <yt/yt/library/tracing/tracer.h>

#include <library/cpp/yt/threading/spin_lock.h>

#include <atomic>

namespace NYT::NTracing {

using namespace NConcurrency;
using namespace NProfiling;
using namespace NYTree;
using namespace NYson;

using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = TracingLogger;

////////////////////////////////////////////////////////////////////////////////

struct TGlobalTracerStorage
{
    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, Lock);
    ITracerPtr Tracer;
};

// TODO(prime@): Switch constinit global variable, once gcc supports it.
static TGlobalTracerStorage* GlobalTracerStorage()
{
    return LeakySingleton<TGlobalTracerStorage>();
}

ITracerPtr GetGlobalTracer()
{
    auto tracerStorage = GlobalTracerStorage();
    auto guard = Guard(tracerStorage->Lock);
    return tracerStorage->Tracer;
}

void SetGlobalTracer(const ITracerPtr& tracer)
{
    ITracerPtr oldTracer;

    {
        auto tracerStorage = GlobalTracerStorage();
        auto guard = Guard(tracerStorage->Lock);
        oldTracer = tracerStorage->Tracer;
        tracerStorage->Tracer = tracer;
    }

    if (oldTracer) {
        oldTracer->Stop();
    }
}

////////////////////////////////////////////////////////////////////////////////

struct TTracingConfigSingleton
{
    TAtomicObject<TTracingConfigPtr> Config = New<TTracingConfig>();
};

static TTracingConfigSingleton* GlobalTracingConfig()
{
    return LeakySingleton<TTracingConfigSingleton>();
}

void SetTracingConfig(TTracingConfigPtr config)
{
    GlobalTracingConfig()->Config.Store(std::move(config));
}

TTracingConfigPtr GetTracingConfig()
{
    return GlobalTracingConfig()->Config.Load();
}

////////////////////////////////////////////////////////////////////////////////

namespace  {

TSpanId GenerateSpanId()
{
    return RandomNumber<ui64>(std::numeric_limits<ui64>::max() - 1) + 1;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

void FormatValue(TStringBuilderBase* builder, const TSpanContext& context, TStringBuf /*spec*/)
{
    builder->AppendFormat("%v:%08" PRIx64 ":%v",
        context.TraceId,
        context.SpanId,
        (context.Sampled ? 1u : 0) | (context.Debug ? 2u : 0));
}

TString ToString(const TSpanContext& context)
{
    return ToStringViaBuilder(context);
}

////////////////////////////////////////////////////////////////////////////////

TTraceContext::TTraceContext(
    TSpanContext parentSpanContext,
    TString spanName,
    TTraceContextPtr parentTraceContext)
    : TraceId_(parentSpanContext.TraceId)
    , SpanId_(GenerateSpanId())
    , ParentSpanId_(parentSpanContext.SpanId)
    , Debug_(parentSpanContext.Debug)
    , State_(parentTraceContext
        ? parentTraceContext->State_.load()
        : (parentSpanContext.Sampled ? ETraceContextState::Sampled : ETraceContextState::Disabled))
    , Propagated_(true)
    , ParentContext_(std::move(parentTraceContext))
    , SpanName_(std::move(spanName))
    , RequestId_(ParentContext_ ? ParentContext_->GetRequestId() : TRequestId{})
    , LoggingTag_(ParentContext_ ? ParentContext_->GetLoggingTag() : TString{})
    , StartTime_(GetCpuInstant())
    , Baggage_(ParentContext_ ? ParentContext_->GetBaggage() : TYsonString{})
{

}

void TTraceContext::SetRequestId(TRequestId requestId)
{
    RequestId_ = requestId;
}

void TTraceContext::SetLoggingTag(const TString& loggingTag)
{
    LoggingTag_ = loggingTag;
}

void TTraceContext::SetAllocationTags(TAllocationTagsPtr tags)
{
    AllocationTags_ = std::move(tags);
}

TAllocationTagsPtr TTraceContext::GetAllocationTags() const
{
    return AllocationTags_;
}

std::vector<std::pair<TString, TString>> TTraceContext::ExtractAllocationTags() const
{
    if (AllocationTags_ != nullptr) {
        return AllocationTags_->GetTags();
    }
    return {};
}

void TTraceContext::SetRecorded()
{
    auto disabled = ETraceContextState::Disabled;
    State_.compare_exchange_strong(disabled, ETraceContextState::Recorded);
}

void TTraceContext::SetPropagated(bool value)
{
    Propagated_ = value;
}

TTraceContextPtr TTraceContext::CreateChild(
    TString spanName)
{
    auto child = New<TTraceContext>(
        GetSpanContext(),
        std::move(spanName),
        /* parentTraceContext */ this);

    auto guard = Guard(Lock_);
    child->ProfilingTags_ = ProfilingTags_;
    return child;
}

TSpanContext TTraceContext::GetSpanContext() const
{
    return TSpanContext{
        .TraceId = GetTraceId(),
        .SpanId = GetSpanId(),
        .Sampled = IsSampled(),
        .Debug = Debug_,
    };
}

TDuration TTraceContext::GetElapsedTime() const
{
    return CpuDurationToDuration(GetElapsedCpuTime());
}

void TTraceContext::SetSampled(bool value)
{
    if (!value) {
        State_ = ETraceContextState::Disabled;
    } else {
        State_ = ETraceContextState::Sampled;
    }
}

TInstant TTraceContext::GetStartTime() const
{
    return NProfiling::CpuInstantToInstant(StartTime_);
}

TDuration TTraceContext::GetDuration() const
{
    YT_ASSERT(Finished_.load());
    return NProfiling::CpuDurationToDuration(Duration_.load());
}

TTraceContext::TTagList TTraceContext::GetTags() const
{
    auto guard = Guard(Lock_);
    return Tags_;
}

TTraceContext::TLogList TTraceContext::GetLogEntries() const
{
    auto guard = Guard(Lock_);
    return Logs_;
}

TTraceContext::TAsyncChildrenList TTraceContext::GetAsyncChildren() const
{
    auto guard = Guard(Lock_);
    return AsyncChildren_;
}

TYsonString TTraceContext::GetBaggage() const
{
    auto guard = Guard(Lock_);
    return Baggage_;
}

void TTraceContext::SetBaggage(TYsonString baggage)
{
    auto guard = Guard(Lock_);
    Baggage_ = std::move(baggage);
}

IAttributeDictionaryPtr TTraceContext::UnpackBaggage() const
{
    auto baggage = GetBaggage();
    return baggage ? ConvertToAttributes(baggage) : nullptr;
}

NYTree::IAttributeDictionaryPtr TTraceContext::UnpackOrCreateBaggage() const
{
    auto baggage = GetBaggage();
    return baggage ? ConvertToAttributes(baggage) : CreateEphemeralAttributes();
}

void TTraceContext::PackBaggage(const IAttributeDictionaryPtr& baggage)
{
    SetBaggage(baggage ? ConvertToYsonString(baggage) : TYsonString{});
}

void TTraceContext::AddTag(const TString& tagKey, const TString& tagValue)
{
    if (!IsRecorded()) {
        return;
    }

    if (Finished_.load()) {
        return;
    }

    auto guard = Guard(Lock_);
    Tags_.emplace_back(tagKey, tagValue);
}

void TTraceContext::AddProfilingTag(const TString& name, const TString& value)
{
    auto guard = Guard(Lock_);
    ProfilingTags_.emplace_back(name, value);
}

void TTraceContext::AddProfilingTag(const TString& name, i64 value)
{
    auto guard = Guard(Lock_);
    ProfilingTags_.emplace_back(name, value);
}

std::vector<std::pair<TString, std::variant<TString, i64>>> TTraceContext::GetProfilingTags()
{
    auto guard = Guard(Lock_);
    return ProfilingTags_;
}

bool TTraceContext::AddAsyncChild(const TTraceId& traceId)
{
    if (!IsRecorded()) {
        return false;
    }

    if (Finished_.load()) {
        return false;
    }

    auto guard = Guard(Lock_);
    AsyncChildren_.push_back(traceId);
    return true;
}

void TTraceContext::AddErrorTag()
{
    if (!IsRecorded()) {
        return;
    }

    static const TString ErrorAnnotationName("error");
    static const TString ErrorAnnotationValue("true");
    AddTag(ErrorAnnotationName, ErrorAnnotationValue);
}

void TTraceContext::AddLogEntry(TCpuInstant at, TString message)
{
    if (!IsRecorded()) {
        return;
    }

    if (Finished_.load()) {
        return;
    }

    auto guard = Guard(Lock_);
    Logs_.push_back(TTraceLogEntry{at, std::move(message)});
}

bool TTraceContext::IsFinished()
{
    return Finished_.load();
}

bool TTraceContext::IsSampled() const
{
    auto traceContext = this;
    while (traceContext) {
        auto state = traceContext->State_.load(std::memory_order_relaxed);
        if (state == ETraceContextState::Sampled) {
            return true;
        } else if (state == ETraceContextState::Disabled) {
            return false;
        }

        traceContext = traceContext->ParentContext_.Get();
    }

    return false;
}

void TTraceContext::SetDuration()
{
    if (Duration_.load() == 0) {
        Duration_ = GetCpuInstant() - StartTime_;
    }
}

void TTraceContext::Finish()
{
    if (Finished_.exchange(true)) {
        return;
    }
    SetDuration();

    auto state = State_.load(std::memory_order_relaxed);
    if (state == ETraceContextState::Disabled) {
        return;
    } else if (state == ETraceContextState::Sampled) {
        if (auto tracer = GetGlobalTracer(); tracer) {
            tracer->Enqueue(MakeStrong(this));
        }
    } else if (state == ETraceContextState::Recorded) {
        if (!IsSampled()) {
            return;
        }

        if (auto tracer = GetGlobalTracer(); tracer) {
            auto traceContext = this;
            while (traceContext) {
                if (traceContext->State_.load() != ETraceContextState::Recorded) {
                    break;
                }

                if (traceContext->Finished_.load() && !traceContext->Submitted_.exchange(true)) {
                    traceContext->SetDuration();
                    tracer->Enqueue(MakeStrong(traceContext));
                }

                traceContext = traceContext->ParentContext_.Get();
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void FormatValue(TStringBuilderBase* builder, const TTraceContextPtr& context, TStringBuf /*spec*/)
{
    if (context) {
        builder->AppendFormat("%v %v",
            context->GetSpanName(),
            context->GetSpanContext());
    } else {
        builder->AppendString(TStringBuf("<null>"));
    }
}

TString ToString(const TTraceContextPtr& context)
{
    return ToStringViaBuilder(context);
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TTracingExt* ext, const TTraceContextPtr& context)
{
    if (!context || !context->IsPropagated()) {
        ext->Clear();
        return;
    }

    ToProto(ext->mutable_trace_id(), context->GetTraceId());
    ext->set_span_id(context->GetSpanId());
    ext->set_sampled(context->IsSampled());
    ext->set_debug(context->IsDebug());

    if (GetTracingConfig()->SendBaggage) {
        if (auto baggage = context->GetBaggage()) {
            ext->set_baggage(baggage.ToString());
        }
    }
}

TTraceContextPtr TTraceContext::NewRoot(TString spanName, TTraceId traceId)
{
    return New<TTraceContext>(
        TSpanContext{
            .TraceId = traceId ? traceId : TTraceId::Create(),
            .SpanId = InvalidSpanId,
            .Sampled = false,
            .Debug = false,
        },
        std::move(spanName));
}

TTraceContextPtr TTraceContext::NewChildFromSpan(
    TSpanContext parentSpanContext,
    TString spanName,
    TYsonString baggage)
{
    auto result = New<TTraceContext>(
        parentSpanContext,
        std::move(spanName));
    result->SetBaggage(std::move(baggage));
    return result;
}

TTraceContextPtr TTraceContext::NewChildFromRpc(
    const NProto::TTracingExt& ext,
    TString spanName,
    TRequestId requestId,
    bool forceTracing)
{
    auto traceId = FromProto<TTraceId>(ext.trace_id());
    if (!traceId) {
        if (!forceTracing) {
            return nullptr;
        }

        auto root = NewRoot(std::move(spanName));
        root->SetRequestId(requestId);
        root->SetRecorded();
        return root;
    }

    auto traceContext = New<TTraceContext>(
        TSpanContext{
            traceId,
            ext.span_id(),
            ext.sampled(),
            ext.debug()
        },
        std::move(spanName));
    traceContext->SetRequestId(requestId);
    if (ext.has_baggage()) {
        traceContext->SetBaggage(TYsonString(ext.baggage()));
    }
    return traceContext;
}

////////////////////////////////////////////////////////////////////////////////

struct TCurrentTraceContextReclaimer
{
    ~TCurrentTraceContextReclaimer()
    {
        if (CurrentTraceContext) {
            auto traceContext = CurrentTraceContext;
            CurrentTraceContext = nullptr;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            traceContext->Unref();
        }
    }
};

thread_local TTraceContext* CurrentTraceContext;
thread_local TCpuInstant TraceContextTimingCheckpoint;
static thread_local TCurrentTraceContextReclaimer CurrentTraceContextReclaimer;

TTraceContextPtr SwitchTraceContext(TTraceContextPtr newContext, NProfiling::TCpuInstant now)
{
    auto oldContext = TTraceContextPtr(CurrentTraceContext, false);

    // Invalid if no oldContext
    auto delta = now - TraceContextTimingCheckpoint;

    if (oldContext && newContext) {
        YT_LOG_TRACE("Switching context (OldContext: %v, NewContext: %v, CpuTimeDelta: %v)",
            oldContext,
            newContext,
            NProfiling::CpuDurationToDuration(delta));
    } else if (oldContext) {
        YT_LOG_TRACE("Uninstalling context (Context: %v, CpuTimeDelta: %v)",
            oldContext,
            NProfiling::CpuDurationToDuration(delta));
    } else if (newContext) {
        YT_LOG_TRACE("Installing context (Context: %v)",
            newContext);
    }

    if (oldContext) {
        oldContext->IncrementElapsedCpuTime(delta);
    }

    CurrentTraceContext = newContext.Release();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    TraceContextTimingCheckpoint = now;

    return oldContext;
}

TTraceContextPtr SwitchTraceContext(TTraceContextPtr newContext)
{
    return SwitchTraceContext(newContext, GetCpuInstant());
}

void FlushCurrentTraceContextTime()
{
    auto* context = static_cast<TTraceContext*>(CurrentTraceContext);
    if (!context) {
        return;
    }

    auto now = GetCpuInstant();
    auto delta = now - TraceContextTimingCheckpoint;
    YT_LOG_TRACE("Flushing context time (Context: %v, CpuTimeDelta: %v)",
        context,
        NProfiling::CpuDurationToDuration(delta));
    context->IncrementElapsedCpuTime(delta);
    TraceContextTimingCheckpoint = now;
}

void TTraceContext::IncrementElapsedCpuTime(NProfiling::TCpuDuration delta)
{
    auto* current = this;
    while (current) {
        current->ElapsedCpuTime_ += delta;
        current = current->ParentContext_.Get();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTracing

////////////////////////////////////////////////////////////////////////////////

namespace NYT::NYTProf {

////////////////////////////////////////////////////////////////////////////////

void* AcquireFiberTagStorage()
{
    auto traceContext = NTracing::CurrentTraceContext;
    if (traceContext) {
        Ref(traceContext);
    }
    return reinterpret_cast<void*>(traceContext);
}

std::vector<std::pair<TString, std::variant<TString, i64>>> ReadFiberTags(void* storage)
{
    auto traceContext = reinterpret_cast<NTracing::TTraceContext*>(storage);
    if (traceContext) {
        return traceContext->GetProfilingTags();
    } else {
        return {};
    }
}

void ReleaseFiberTagStorage(void* storage)
{
    if (storage) {
        Unref(reinterpret_cast<NTracing::TTraceContext*>(storage));
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYTProf
