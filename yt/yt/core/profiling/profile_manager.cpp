#include "config.h"
#include "profile_manager.h"
#include "timing.h"

#include <yt/yt/core/concurrency/profiling_helpers.h>
#include <yt/yt/core/concurrency/periodic_executor.h>
#include <yt/yt/core/concurrency/scheduler_thread.h>
#include <yt/yt/core/concurrency/invoker_queue.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/misc/hash.h>
#include <yt/yt/core/misc/id_generator.h>
#include <yt/yt/core/misc/mpsc_stack.h>
#include <yt/yt/core/misc/singleton.h>

#include <yt/yt/core/ytree/ephemeral_node_factory.h>
#include <yt/yt/core/ytree/fluent.h>
#include <yt/yt/core/ytree/node.h>
#include <yt/yt/core/ytree/virtual.h>
#include <yt/yt/core/ytree/ypath_client.h>
#include <yt/yt/core/ytree/ypath_detail.h>

#include <yt/yt/library/profiling/resource_tracker/resource_tracker.h>

#include <library/cpp/yt/threading/fork_aware_spin_lock.h>

#include <util/generic/iterator_range.h>

namespace NYT::NProfiling {

using namespace NYTree;
using namespace NYson;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

static inline const NLogging::TLogger Logger("Profiling");

////////////////////////////////////////////////////////////////////////////////

class TProfileManager::TImpl
    : public TRefCounted
{
public:
    TImpl()
        : WasStarted_(false)
        , WasStopped_(false)
        , EventQueue_(New<TMpscInvokerQueue>(
            EventCount_,
            NConcurrency::GetThreadTags("Profiler")))
        , Thread_(New<TThread>(this))
        , Root_(GetEphemeralNodeFactory(true)->CreateMap())
    {
        ResourceTracker_ = New<TResourceTracker>();
    }

    void Start()
    {
        YT_VERIFY(!WasStarted_);
        YT_VERIFY(!WasStopped_);

        WasStarted_ = true;

        Thread_->Start();
        EventQueue_->SetThreadId(Thread_->GetThreadId());

        DequeueExecutor_ = New<TPeriodicExecutor>(
            EventQueue_,
            BIND(&TImpl::OnDequeue, MakeStrong(this)),
            Config_->DequeuePeriod);
        DequeueExecutor_->Start();
    }

    void Stop()
    {
        WasStopped_ = true;
        EventQueue_->Shutdown();
        Thread_->Stop();
    }

    void Enqueue(const TQueuedSample& sample)
    {
        if (!WasStarted_ || WasStopped_) {
            return;
        }

        for (auto id : sample.TagIds) {
            if (id < 0 || id >= RegisteredTagCount_) {
                YT_LOG_ALERT("Invalid tag id in profiling sample (Path: %v, TagId: %v)",
                    sample.Path,
                    id);
                return;
            }
        }

        SampleQueue_.Enqueue(sample);
    }

    void Configure(const TProfileManagerConfigPtr& config)
    {
        GlobalTags_ = config->GlobalTags;
        Config_ = config;
    }

    IInvokerPtr GetInvoker() const
    {
        return EventQueue_;
    }

    IMapNodePtr GetRoot() const
    {
        return Root_;
    }

    IYPathServicePtr GetService() const
    {
        return GetRoot()->Via(GetInvoker());
    }

    TProfileManagerConfigPtr GetConfig() const
    {
        return Config_;
    }

    TTagId RegisterTag(const TStringTag& tag)
    {
        auto guard = Guard(TagSpinLock_);
        auto pair = std::make_pair(tag.Key, tag.Value);
        auto it = TagToId_.find(pair);
        if (it != TagToId_.end()) {
            return it->second;
        }

        auto id = static_cast<TTagId>(IdToTag_.size());
        IdToTag_.push_back(tag);
        YT_VERIFY(TagToId_.emplace(pair, id).second);
        TagKeyToValues_[tag.Key].push_back(tag.Value);
        ++RegisteredTagCount_;

        return id;
    }

    TStringTag LookupTag(TTagId tag)
    {
        auto guard = Guard(TagSpinLock_);
        return GetTag(tag);
    }

    NThreading::TForkAwareSpinLock& GetTagSpinLock()
    {
        return TagSpinLock_;
    }

    const TStringTag& GetTag(TTagId id)
    {
        return IdToTag_[id];
    }

    void SetGlobalTag(TTagId id)
    {
        BIND([this_ = MakeStrong(this), this, id] () {
            const auto& newTag = GetTag(id);
            for (auto& tagId : GlobalTagIds_) {
                // Replace tag with the same key.
                if (GetTag(tagId).Key == newTag.Key) {
                    tagId = id;
                    return;
                }
            }
            GlobalTagIds_.push_back(id);
        })
            .Via(GetInvoker())
            .Run();
    }

    TResourceTrackerPtr GetResourceTracker() const
    {
        return ResourceTracker_;
    }

private:
    struct TStoredSample
    {
        i64 Id;
        TInstant Time;
        TValue Value;
        TTagIdList TagIds;
        EMetricType MetricType;
        NYPath::TYPath Path;
    };

    class TSampleStorage
    {
    public:
        using TSamples = std::deque<TStoredSample>;
        using TSamplesIterator = TSamples::iterator;
        using TSampleRange = TIteratorRange<TSamplesIterator>;
        using TSampleIdAndRange = std::pair<i64, TSampleRange>;

        //! Adds new sample to the deque.
        void AddSample(const TStoredSample& sample)
        {
            Samples_.push_back(sample);
        }

        //! Gets samples with #id more than #count in the deque and id of the first returned element.
        //! If #count is not given, all deque is returned.
        TSampleIdAndRange GetSamples(std::optional<i64> count = std::nullopt)
        {
            if (!count) {
                return std::make_pair(RemovedCount_, TSampleRange(Samples_.begin(), Samples_.end()));
            }
            if (*count > std::ssize(Samples_) + RemovedCount_) {
                return std::make_pair(*count, TSampleRange(Samples_.end(), Samples_.end()));
            } else {
                auto startIndex = std::max(*count - RemovedCount_, i64(0));
                return std::make_pair(startIndex + RemovedCount_,
                    TSampleRange(Samples_.begin() + startIndex, Samples_.end()));
            }
        }

        //! Removes old samples (#count instances from the beginning of the deque).
        void RemoveOldSamples(TDuration maxKeepInterval)
        {
            if (Samples_.size() <= 1) {
                return;
            }
            auto deadline = Samples_.back().Time - maxKeepInterval;
            while (Samples_.front().Time < deadline) {
                ++RemovedCount_;
                Samples_.pop_front();
            }
        }

    private:
        TSamples Samples_;
        THashMap<TTagId, TStringTag> TagIdToValue_;
        i64 RemovedCount_ = 0;
    };

    class TBucket
        : public TYPathServiceBase
        , public TSupportsGet
    {
    public:
        typedef std::deque<TStoredSample> TSamples;
        typedef TSamples::iterator TSamplesIterator;
        typedef std::pair<TSamplesIterator, TSamplesIterator> TSamplesRange;

        TBucket(const THashMap<TString, TString>& globalTags, const TProfileManagerConfigPtr& config)
            : GlobalTags_(globalTags)
            , Config_(config)
        { }

        //! Adds a new sample to the bucket inserting in at an appropriate position.
        int AddSample(const TStoredSample& sample)
        {
            auto& rateLimit = RateLimits[sample.TagIds];
            if (rateLimit.first && sample.Time < rateLimit.first + Config_->SampleRateLimit) {
                ++rateLimit.second;
                return rateLimit.second;
            }
            rateLimit.first = sample.Time;
            rateLimit.second = 0;

            // Samples are ordered by time.
            // Search for an appropriate insertion point starting from the the back,
            // this should usually be fast.
            int index = static_cast<int>(Samples.size());
            while (index > 0 && Samples[index - 1].Time > sample.Time) {
                --index;
            }
            Samples.insert(Samples.begin() + index, sample);
            return 0;
        }

        //! Removes the oldest samples keeping [minTime,maxTime] interval no larger than #maxKeepInterval.
        void TrimSamples(TDuration maxKeepInterval)
        {
            if (Samples.size() <= 1)
                return;

            auto deadline = Samples.back().Time - maxKeepInterval;
            while (Samples.front().Time < deadline) {
                Samples.pop_front();
            }
        }

        //! Gets samples with timestamp larger than #lastTime.
        //! If #lastTime is null then all samples are returned.
        TSamplesRange GetSamples(std::optional<TInstant> lastTime = std::nullopt)
        {
            if (!lastTime) {
                return make_pair(Samples.begin(), Samples.end());
            }

            // Run binary search to find the proper position.
            TStoredSample lastSample;
            lastSample.Time = *lastTime;
            auto it = std::upper_bound(
                Samples.begin(),
                Samples.end(),
                lastSample,
                [=] (const TStoredSample& lhs, const TStoredSample& rhs) { return lhs.Time < rhs.Time; });

            return std::make_pair(it, Samples.end());
        }

    private:
        std::deque<TStoredSample> Samples;
        std::map<TTagIdList, std::pair<TInstant, int>> RateLimits;
        THashMap<TString, TString> GlobalTags_;
        TProfileManagerConfigPtr Config_;

        bool DoInvoke(const NRpc::IServiceContextPtr& context) override
        {
            DISPATCH_YPATH_SERVICE_METHOD(Get);
            return TYPathServiceBase::DoInvoke(context);
        }

        static std::optional<TInstant> ParseInstant(std::optional<i64> value)
        {
            return value ? std::make_optional(TInstant::MicroSeconds(*value)) : std::nullopt;
        }

        void GetSelf(
            TReqGet* request,
            TRspGet* response,
            const TCtxGetPtr& context) override
        {
            auto options = FromProto(request->options());
            auto fromTime = ParseInstant(options->Find<i64>("from_time"));
            context->SetRequestInfo("FromTime: %v", fromTime);

            auto samplesRange = GetSamples(fromTime);

            THashMap<TTagId, TStringTag> tagIdToValue;
            for (auto it = samplesRange.first; it != samplesRange.second; ++it) {
                const auto& sample = *it;
                for (auto tagId : sample.TagIds) {
                    tagIdToValue.emplace(tagId, TStringTag());
                }
            }

            {
                const auto& profilingManager = TProfileManager::Get()->Impl_;
                auto tagGuard = Guard(profilingManager->GetTagSpinLock());
                for (auto& [tagId, tag] : tagIdToValue) {
                    tag = profilingManager->GetTag(tagId);
                }
            }

            response->set_value(BuildYsonStringFluently()
                .DoListFor(samplesRange.first, samplesRange.second, [&] (TFluentList fluent, const TSamplesIterator& it) {
                    const auto& sample = *it;
                    fluent
                        .Item().BeginMap()
                            .Item("id").Value(sample.Id)
                            .Item("time").Value(static_cast<i64>(sample.Time.MicroSeconds()))
                            .Item("value").Value(sample.Value)
                            .Item("tags").BeginMap()
                                .DoFor(GlobalTags_, [&] (TFluentMap fluent, const std::pair<const TString, TString>& tag) {
                                    fluent
                                        .Item(tag.first).Value(tag.second);
                                })
                                .DoFor(sample.TagIds, [&] (TFluentMap fluent, TTagId tagId) {
                                    const auto& tag = GetOrCrash(tagIdToValue, tagId);
                                    fluent
                                        .Item(tag.Key).Value(tag.Value);
                                })
                            .EndMap()
                            .Item("metric_type").Value(sample.MetricType)
                        .EndMap();
                }).ToString());

            context->Reply();
        }
    };

    typedef TIntrusivePtr<TBucket> TBucketPtr;

    class TThread
        : public TSchedulerThread
    {
    public:
        explicit TThread(TImpl* owner)
            : TSchedulerThread(
                owner->EventCount_,
                "Profiling",
                "Profiling")
            , Owner(owner)
        { }

    private:
        TImpl* const Owner;

        TClosure BeginExecute() override
        {
            return Owner->BeginExecute();
        }

        void EndExecute() override
        {
            Owner->EndExecute();
        }
    };

    const TIntrusivePtr<NThreading::TEventCount> EventCount_ = New<NThreading::TEventCount>();
    std::atomic<bool> WasStarted_;
    std::atomic<bool> WasStopped_;
    TMpscInvokerQueuePtr EventQueue_;
    TIntrusivePtr<TThread> Thread_;
    TEnqueuedAction CurrentAction_;

    TPeriodicExecutorPtr DequeueExecutor_;

    const IMapNodePtr Root_;

    TProfileManagerConfigPtr Config_;

    TMpscStack<TQueuedSample> SampleQueue_;
    THashMap<TYPath, TBucketPtr> PathToBucket_;
    TIdGenerator SampleIdGenerator_;

    NThreading::TForkAwareSpinLock TagSpinLock_;
    std::vector<TStringTag> IdToTag_;
    std::atomic<int> RegisteredTagCount_ = 0;
    THashMap<std::pair<TString, TString>, int> TagToId_;
    using TTagKeyToValues = THashMap<TString, std::vector<TString>>;
    TTagKeyToValues TagKeyToValues_;

    //! Tags attached to every sample.
    TTagIdList GlobalTagIds_;

    //! One deque instead of buckets with deques.
    TSampleStorage Storage_;

    TIntrusivePtr<TResourceTracker> ResourceTracker_;

    THashMap<TString, TString> GlobalTags_;

    TClosure BeginExecute()
    {
        return EventQueue_->BeginExecute(&CurrentAction_);
    }

    void EndExecute()
    {
        EventQueue_->EndExecute(&CurrentAction_);
    }

    void OnDequeue()
    {
        // Process all pending samples in a row.
        int samplesProcessed = 0;

        while (SampleQueue_.DequeueAll(true, [&] (TQueuedSample& sample) {
                try {
                    // Enrich sample with global tags.
                    sample.TagIds.insert(sample.TagIds.end(), GlobalTagIds_.begin(), GlobalTagIds_.end());
                    ProcessSample(sample);
                    ProcessSampleV2(sample);
                    ++samplesProcessed;
                } catch (const std::exception& ex) {
                    THROW_ERROR_EXCEPTION("Failed to process sample") << ex
                        << TErrorAttribute("path", sample.Path);
                }
            }))
        { }
    }

    TBucketPtr LookupBucket(const TYPath& path)
    {
        auto it = PathToBucket_.find(path);
        if (it != PathToBucket_.end()) {
            return it->second;
        }

        YT_LOG_DEBUG("Creating bucket %v", path);
        auto bucket = New<TBucket>(GlobalTags_, Config_);
        YT_VERIFY(PathToBucket_.emplace(path, bucket).second);

        auto node = CreateVirtualNode(bucket);
        ForceYPath(Root_, path);
        SetNodeByYPath(Root_, path, node);

        return bucket;
    }

    void ProcessSample(const TQueuedSample& queuedSample)
    {
        auto bucket = LookupBucket(queuedSample.Path);

        TStoredSample storedSample;
        storedSample.Id = SampleIdGenerator_.Next();
        storedSample.Time = CpuInstantToInstant(queuedSample.Time);
        storedSample.Value = queuedSample.Value;
        storedSample.TagIds = queuedSample.TagIds;
        storedSample.MetricType = queuedSample.MetricType;

        if (bucket->AddSample(storedSample) == 1) {
            THashMultiMap<TString, TString> tags;
            {
                auto guard = Guard(TagSpinLock_);
                for (auto tagId : storedSample.TagIds) {
                    const auto& tag = GetTag(tagId);
                    tags.emplace(tag.Key, tag.Value);
                }
            }
            YT_LOG_DEBUG("Profiling sample dropped (Path: %v, Tags: %v)",
                queuedSample.Path,
                tags);
        }
        bucket->TrimSamples(Config_->MaxKeepInterval);
    }

    void ProcessSampleV2(const TQueuedSample& queuedSample)
    {
        TStoredSample storedSample;
        storedSample.Id = 0;
        storedSample.Time = CpuInstantToInstant(queuedSample.Time);
        storedSample.Value = queuedSample.Value;
        storedSample.TagIds = queuedSample.TagIds;
        storedSample.MetricType = queuedSample.MetricType;
        storedSample.Path = queuedSample.Path;
        Storage_.AddSample(storedSample);
        Storage_.RemoveOldSamples(Config_->MaxKeepInterval);
    }
};

////////////////////////////////////////////////////////////////////////////////

TProfileManager::TProfileManager()
    : Impl_(New<TImpl>())
{ }

TProfileManager::~TProfileManager() = default;

TProfileManager* TProfileManager::Get()
{
    return Singleton<TProfileManager>();
}

void TProfileManager::Configure(const TProfileManagerConfigPtr& config)
{
    Impl_->Configure(config);
}

void TProfileManager::Start()
{
    Impl_->Start();
}

void TProfileManager::Stop()
{
    Impl_->Stop();
}

void TProfileManager::Enqueue(const TQueuedSample& sample)
{
    Impl_->Enqueue(sample);
}

IInvokerPtr TProfileManager::GetInvoker() const
{
    return Impl_->GetInvoker();
}

IMapNodePtr TProfileManager::GetRoot() const
{
    return Impl_->GetRoot();
}

IYPathServicePtr TProfileManager::GetService() const
{
    return Impl_->GetService();
}

TTagId TProfileManager::RegisterTag(const TStringTag& tag)
{
    return Impl_->RegisterTag(tag);
}

TStringTag TProfileManager::LookupTag(TTagId tag)
{
    return Impl_->LookupTag(tag);
}

void TProfileManager::SetGlobalTag(TTagId id)
{
    Impl_->SetGlobalTag(id);
}

TResourceTrackerPtr TProfileManager::GetResourceTracker() const
{
    return Impl_->GetResourceTracker();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NProfiling
