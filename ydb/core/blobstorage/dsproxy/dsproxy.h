#pragma once

#include "defs.h"
#include "dsproxy_mon.h"
#include "dsproxy_responsiveness.h"
#include "log_acc.h"
#include "group_sessions.h"

#include <ydb/core/blobstorage/lwtrace_probes/blobstorage_probes.h>
#include <ydb/core/blobstorage/groupinfo/blobstorage_groupinfo.h>
#include <ydb/core/blobstorage/storagepoolmon/storagepool_counters.h>
#include <ydb/core/blobstorage/vdisk/ingress/blobstorage_ingress.h>
#include <ydb/core/blobstorage/base/batched_vec.h>
#include <ydb/core/blobstorage/base/blobstorage_events.h>
#include <ydb/core/blobstorage/base/transparent.h>
#include <ydb/core/blobstorage/base/wilson_events.h>
#include <ydb/core/blobstorage/backpressure/queue_backpressure_client.h>
#include <library/cpp/actors/core/interconnect.h>
#include <ydb/core/base/appdata.h>
#include <ydb/core/base/group_stat.h>
#include <library/cpp/containers/stack_vector/stack_vec.h>
#include <util/generic/hash_set.h>

namespace NKikimr {

LWTRACE_USING(BLOBSTORAGE_PROVIDER);

constexpr ui32 TypicalPartsInBlob = 6;
constexpr ui32 TypicalDisksInSubring = 8;

constexpr ui32 MaxBatchedPutSize = 64 * 1024 - 512 - 5; // (MinREALHugeBlobInBytes - 1 - TDiskBlob::HugeBlobOverhead) for ssd and nvme

const TDuration ProxyConfigurationTimeout = TDuration::Seconds(20);
const ui32 ProxyRetryConfigurationInitialTimeout = 200;
const ui32 ProxyRetryConfigurationMaxTimeout = 5000;
const ui64 UnconfiguredBufferSizeLimit = 32 << 20;

const TDuration ProxyEstablishSessionsTimeout = TDuration::Seconds(100);

const ui64 DsPutWakeupMs = 60000;

const ui64 BufferSizeThreshold = 1 << 20;

const bool IsHandoffAccelerationEnabled = false;

const bool IsEarlyRequestAbortEnabled = false;
const bool IngressAsAReasonForErrorEnabled = false;

const ui32 BeginRequestSize = 10;
const ui32 MaxRequestSize = 1000;

const ui32 MaskSizeBits = 32;

constexpr bool DefaultEnablePutBatching = true;
constexpr bool DefaultEnableVPatch = false;

constexpr bool WithMovingPatchRequestToStaticNode = true;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Common types
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TEvDeathNote : public TEventLocal<TEvDeathNote, TEvBlobStorage::EvDeathNote> {
    TStackVec<std::pair<TDiskResponsivenessTracker::TDiskId, TDuration>, 16> Responsiveness;

    TEvDeathNote(const TStackVec<std::pair<TDiskResponsivenessTracker::TDiskId, TDuration>, 16> &responsiveness)
        : Responsiveness(responsiveness)
    {}
};

struct TEvAbortOperation : public TEventLocal<TEvAbortOperation, TEvBlobStorage::EvAbortOperation>
{};

struct TEvLatencyReport : public TEventLocal<TEvLatencyReport, TEvBlobStorage::EvLatencyReport> {
    TGroupStat::EKind Kind;
    TDuration Sample;

    TEvLatencyReport(TGroupStat::EKind kind, TDuration sample)
        : Kind(kind)
        , Sample(sample)
    {}
};

struct TNodeLayoutInfo : TThrRefBase {
    // indexed by NodeId
    TNodeLocation SelfLocation;
    TVector<TNodeLocation> LocationPerOrderNumber;

    TNodeLayoutInfo(const TNodeLocation& selfLocation, const TIntrusivePtr<TBlobStorageGroupInfo>& info,
            std::unordered_map<ui32, TNodeLocation>& map)
        : SelfLocation(selfLocation)
        , LocationPerOrderNumber(info->GetTotalVDisksNum())
    {
        for (ui32 i = 0; i < LocationPerOrderNumber.size(); ++i) {
            LocationPerOrderNumber[i] = map[info->GetActorId(i).NodeId()];
        }
    }
};

using TNodeLayoutInfoPtr = TIntrusivePtr<TNodeLayoutInfo>;

inline TStoragePoolCounters::EHandleClass HandleClassToHandleClass(NKikimrBlobStorage::EGetHandleClass handleClass) {
    switch (handleClass) {
        case NKikimrBlobStorage::FastRead:
            return TStoragePoolCounters::EHandleClass::HcGetFast;
        case NKikimrBlobStorage::AsyncRead:
            return TStoragePoolCounters::EHandleClass::HcGetAsync;
        case NKikimrBlobStorage::Discover:
            return TStoragePoolCounters::EHandleClass::HcGetDiscover;
        case NKikimrBlobStorage::LowRead:
            return TStoragePoolCounters::EHandleClass::HcGetLow;
    }
    return TStoragePoolCounters::EHandleClass::HcCount;
}

inline TStoragePoolCounters::EHandleClass HandleClassToHandleClass(NKikimrBlobStorage::EPutHandleClass handleClass) {
    switch (handleClass) {
        case NKikimrBlobStorage::TabletLog:
            return TStoragePoolCounters::EHandleClass::HcPutTabletLog;
        case NKikimrBlobStorage::UserData:
            return TStoragePoolCounters::EHandleClass::HcPutUserData;
        case NKikimrBlobStorage::AsyncBlob:
            return TStoragePoolCounters::EHandleClass::HcPutAsync;
    }
    return TStoragePoolCounters::EHandleClass::HcCount;
}

NActors::NLog::EPriority PriorityForStatusOutbound(NKikimrProto::EReplyStatus status);
NActors::NLog::EPriority PriorityForStatusResult(NKikimrProto::EReplyStatus status);
NActors::NLog::EPriority PriorityForStatusInbound(NKikimrProto::EReplyStatus status);

template<typename TDerived>
class TBlobStorageGroupRequestActor : public TActorBootstrapped<TDerived> {
public:
    static constexpr NKikimrServices::TActivity::EType ActorActivityType() {
        return NKikimrServices::TActivity::BS_GROUP_REQUEST;
    }

    TBlobStorageGroupRequestActor(TIntrusivePtr<TBlobStorageGroupInfo> info, TIntrusivePtr<TGroupQueues> groupQueues,
            TIntrusivePtr<TBlobStorageGroupProxyMon> mon, const TActorId& source, ui64 cookie, NWilson::TTraceId traceId,
            NKikimrServices::EServiceKikimr logComponent, bool logAccEnabled, TMaybe<TGroupStat::EKind> latencyQueueKind,
            TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters, ui32 restartCounter)
        : Info(std::move(info))
        , GroupQueues(std::move(groupQueues))
        , Mon(std::move(mon))
        , PoolCounters(storagePoolCounters)
        , LogCtx(logComponent, logAccEnabled)
        , TraceId(std::move(traceId))
        , RestartCounter(restartCounter)
        , Source(source)
        , Cookie(cookie)
        , LatencyQueueKind(latencyQueueKind)
        , RequestStartTime(now)
        , RacingDomains(&Info->GetTopology())
    {
        TDerived::ActiveCounter(Mon)->Inc();
    }

    template<typename T>
    void CountEvent(const T &ev) const {
        ERequestType request = TDerived::RequestType();
        Mon->CountEvent(request, ev);
    }

    TActorId GetVDiskActorId(const TVDiskIdShort &shortId) const {
        return Info->GetActorId(shortId);
    }

    template<typename TEvent>
    bool CheckForTermErrors(TAutoPtr<TEventHandle<TEvent>>& ev) {
        auto& record = ev->Get()->Record;
        auto& self = Derived();

        if (!record.HasStatus()) {
            return false; // we do not consider messages with missing status/vdisk fields
        }

        NKikimrProto::EReplyStatus status = record.GetStatus(); // obtain status from the reply

        if (status == NKikimrProto::NOTREADY) { // special case from BS_QUEUE -- when connection is not yet established
            record.SetStatus(NKikimrProto::ERROR); // rewrite this status as error for processing
            PostponedQ.emplace_back(ev.Release());
            CheckPostponedQueue();
            return true; // event has been processed early
        }

        if (!record.HasVDiskID()) {
            return false; // bad reply?
        }

        auto done = [&](NKikimrProto::EReplyStatus status, const TString& message) {
            ErrorReason = message;
            A_LOG_LOG_S(true, PriorityForStatusResult(status), "DSP10", "Query failed " << message);
            self.ReplyAndDie(status);
            return true;
        };

        // sanity check for matching group id
        const TVDiskID& vdiskId = VDiskIDFromVDiskID(record.GetVDiskID());
        if (vdiskId.GroupID != Info->GroupID) {
            return done(NKikimrProto::ERROR, TStringBuilder() << "incorrect VDiskId# " << vdiskId << " GroupId# "
                << Info->GroupID);
        }

        // sanity check for correct VDisk generation ??? possible race
        Y_VERIFY_S(status == NKikimrProto::RACE || vdiskId.GroupGeneration <= Info->GroupGeneration ||
            TEvent::EventType == TEvBlobStorage::EvVStatusResult,
            "status# " << NKikimrProto::EReplyStatus_Name(status) << " vdiskId.GroupGeneration# " << vdiskId.GroupGeneration
            << " Info->GroupGeneration# " << Info->GroupGeneration);

        if (status != NKikimrProto::RACE && status != NKikimrProto::BLOCKED && status != NKikimrProto::DEADLINE) {
            return false; // these statuses are non-terminal
        } else if (status != NKikimrProto::RACE) {
            // this status is terminal and we have nothing to do about it
            return done(status, TStringBuilder() << "status# " << NKikimrProto::EReplyStatus_Name(status) << " from# "
                << vdiskId.ToString());
        }

        A_LOG_INFO_S("DSP99", "Handing RACE response from " << vdiskId << " GroupGeneration# " << Info->GroupGeneration
            << " Response# " << SingleLineProto(record));

        // process the RACE status
        const TActorId& nodeWardenId = MakeBlobStorageNodeWardenID(self.SelfId().NodeId());
        if (vdiskId.GroupGeneration < Info->GroupGeneration) { // vdisk is older than our group
            RacingDomains |= {&Info->GetTopology(), vdiskId};
            if (Info->GetQuorumChecker().CheckFailModelForGroupDomains(RacingDomains)) {
                record.SetStatus(NKikimrProto::ERROR);
                auto adjustStatus = [](auto *v) {
                    for (int i = 0; i < v->size(); ++i) {
                        auto *p = v->Mutable(i);
                        if (p->GetStatus() == NKikimrProto::RACE) {
                            p->SetStatus(NKikimrProto::ERROR);
                        }
                    }
                };
                if constexpr (std::is_same_v<TEvent, TEvBlobStorage::TEvVGetResult>) {
                    adjustStatus(record.MutableResult());
                } else if constexpr (std::is_same_v<TEvent, TEvBlobStorage::TEvVMultiPutResult>) {
                    adjustStatus(record.MutableItems());
                }
                return false;
            }
        } else if (Info->GroupGeneration < vdiskId.GroupGeneration) { // our config is older that vdisk's one
            std::optional<NKikimrBlobStorage::TGroupInfo> group;
            if (record.HasRecentGroup()) {
                group = record.GetRecentGroup();
                if (group->GetGroupID() != Info->GroupID || group->GetGroupGeneration() != vdiskId.GroupGeneration) {
                    return done(NKikimrProto::ERROR, "incorrect RecentGroup for RACE response");
                }
            }
            self.Send(nodeWardenId, new TEvBlobStorage::TEvUpdateGroupInfo(vdiskId.GroupID, vdiskId.GroupGeneration,
                std::move(group)));
        }

        // make NodeWarden restart the query just after proxy reconfiguration
        const TActorId& proxyId = GetProxyActorId();
        Y_VERIFY_DEBUG(RestartCounter < 100);
        auto q = self.RestartQuery(RestartCounter + 1);
        ++*Mon->NodeMon->RestartHisto[Min<size_t>(Mon->NodeMon->RestartHisto.size() - 1, RestartCounter)];
        TActivationContext::Send(new IEventHandle(nodeWardenId, Source, q.release(), 0, Cookie, &proxyId, std::move(TraceId)));
        PassAway();
        return true;
    }

    bool ProcessEvent(TAutoPtr<IEventHandle>& ev) {
        switch (ev->GetTypeRewrite()) {
#define CHECK(T) case TEvBlobStorage::T::EventType: return CheckForTermErrors(reinterpret_cast<TEvBlobStorage::T::TPtr&>(ev))
            CHECK(TEvVPutResult);
            CHECK(TEvVMultiPutResult);
            CHECK(TEvVGetResult);
            CHECK(TEvVBlockResult);
            CHECK(TEvVGetBlockResult);
            CHECK(TEvVCollectGarbageResult);
            CHECK(TEvVGetBarrierResult);
            CHECK(TEvVStatusResult);
#undef CHECK

            case TEvBlobStorage::EvProxySessionsState: {
                GroupQueues = static_cast<TEvProxySessionsState*>(ev->GetBase())->GroupQueues;
                return true;
            }

            case TEvAbortOperation::EventType: {
                if (IsEarlyRequestAbortEnabled) {
                    ErrorReason = "Request got EvAbortOperation, IsEarlyRequestAbortEnabled# true";
                    Derived().ReplyAndDie(NKikimrProto::ERROR);
                }
                return true;
            }

            case TEvents::TSystem::Poison: {
                ErrorReason = "Request got Poison";
                Derived().ReplyAndDie(NKikimrProto::ERROR);
                return true;
            }
        }

        return false;
    }

    void CountPuts(const TDeque<std::unique_ptr<TEvBlobStorage::TEvVPut>>& q) {
        for (const auto& item : q) {
            ++GeneratedSubrequests;
            GeneratedSubrequestBytes += item->GetBufferBytes();
        }
    }

    void CountPuts(const TDeque<std::unique_ptr<TEvBlobStorage::TEvVMultiPut>>& q) {
        for (const auto& item : q) {
            ++GeneratedSubrequests;
            GeneratedSubrequestBytes += item->GetBufferBytes();
        }
    }

    template<typename T>
    void SendToQueue(std::unique_ptr<T> event, ui64 cookie, NWilson::TTraceId traceId, bool timeStatsEnabled = false) {
        if constexpr (!std::is_same_v<T, TEvBlobStorage::TEvVStatus>) {
            event->MessageRelevanceTracker = MessageRelevanceTracker;
        }
        const TActorId queueId = GroupQueues->Send(*this, Info->GetTopology(), std::move(event), cookie, std::move(traceId),
            timeStatsEnabled);
        ++RequestsInFlight;
    }

    template<typename TPtr>
    void ProcessReplyFromQueue(const TPtr& /*ev*/) {
        Y_VERIFY(RequestsInFlight);
        --RequestsInFlight;
        CheckPostponedQueue();
    }

    void SendToQueues(TDeque<std::unique_ptr<TEvBlobStorage::TEvVGet>> &vGets, bool timeStatsEnabled) {
        for (auto& request : vGets) {
            WILSON_TRACE_FROM_ACTOR(*TlsActivationContext, *this, &TraceId, EvVGetSent);
            Y_VERIFY(request->Record.HasCookie());
            ui64 messageCookie = request->Record.GetCookie();
            CountEvent(*request);
            const ui64 cyclesPerUs = NHPTimer::GetCyclesPerSecond() / 1000000;
            request->Record.MutableTimestamps()->SetSentByDSProxyUs(GetCycleCountFast() / cyclesPerUs);
            SendToQueue(std::move(request), messageCookie, TraceId.SeparateBranch(), timeStatsEnabled);
        }
    }

    TLogoBlobID GetBlobId(std::unique_ptr<TEvBlobStorage::TEvVPut> &ev) {
        Y_VERIFY(ev->Record.HasBlobID());
        return LogoBlobIDFromLogoBlobID(ev->Record.GetBlobID());
    }

    TLogoBlobID GetBlobId(std::unique_ptr<TEvBlobStorage::TEvVMultiPut> &ev) {
        Y_VERIFY(ev->Record.ItemsSize());
        return LogoBlobIDFromLogoBlobID(ev->Record.GetItems(0).GetBlobID());
    }

    TLogoBlobID GetBlobId(std::unique_ptr<TEvBlobStorage::TEvVMovedPatch> &ev) {
        Y_VERIFY(ev->Record.HasPatchedBlobId());
        return LogoBlobIDFromLogoBlobID(ev->Record.GetPatchedBlobId());
    }

    TLogoBlobID GetBlobId(std::unique_ptr<TEvBlobStorage::TEvVPatchStart> &ev) {
        Y_VERIFY(ev->Record.HasOriginalBlobId());
        return LogoBlobIDFromLogoBlobID(ev->Record.GetOriginalBlobId());
    }

    TLogoBlobID GetBlobId(std::unique_ptr<TEvBlobStorage::TEvVPatchDiff> &ev) {
        Y_VERIFY(ev->Record.HasPatchedPartBlobId());
        return LogoBlobIDFromLogoBlobID(ev->Record.GetPatchedPartBlobId());
    }

    template <typename TEvent>
    void SendToQueues(TDeque<std::unique_ptr<TEvent>> &events, bool timeStatsEnabled) {
        for (auto& request : events) {
            WILSON_TRACE_FROM_ACTOR(*TlsActivationContext, *this, &TraceId, EvVPutSent);
            Y_VERIFY(request->Record.HasCookie());
            ui64 messageCookie = request->Record.GetCookie();
            CountEvent(*request);
            const ui64 cyclesPerUs = NHPTimer::GetCyclesPerSecond() / 1000000;
            request->Record.MutableTimestamps()->SetSentByDSProxyUs(GetCycleCountFast() / cyclesPerUs);
            TLogoBlobID id = GetBlobId(request);
            TVDiskID vDiskId = VDiskIDFromVDiskID(request->Record.GetVDiskID());
            LWTRACK(DSProxyPutVPutIsSent, request->Orbit, Info->GetFailDomainOrderNumber(vDiskId),
                    Info->GroupID, id.Channel(), id.PartId(), id.ToString(), id.BlobSize());
            SendToQueue(std::move(request), messageCookie, TraceId.SeparateBranch(), timeStatsEnabled);
        }
    }

    void SendResponseAndDie(std::unique_ptr<IEventBase>&& ev, TBlobStorageGroupProxyTimeStats *timeStats, TActorId source,
            ui64 cookie, NWilson::TTraceId traceId) {
        SendResponse(std::move(ev), timeStats, source, cookie, std::move(traceId));
        PassAway();
    }

    void SendResponseAndDie(std::unique_ptr<IEventBase>&& ev, TBlobStorageGroupProxyTimeStats *timeStats = nullptr) {
        SendResponseAndDie(std::move(ev), timeStats, Source, Cookie, std::move(TraceId));
    }

    TActorId GetProxyActorId() const {
        return MakeBlobStorageProxyID(Info->GroupID);
    }

    void PassAway() override {
        // ensure that we are dying for the first time
        Y_VERIFY(!std::exchange(Dead, true));
        TDerived::ActiveCounter(Mon)->Dec();
        Derived().Send(GetProxyActorId(), new TEvDeathNote(Responsiveness));
        TActorBootstrapped<TDerived>::PassAway();
    }

    void SendResponse(std::unique_ptr<IEventBase>&& ev, TBlobStorageGroupProxyTimeStats *timeStats, TActorId source, ui64 cookie,
            NWilson::TTraceId traceId) {
        const TInstant now = TActivationContext::Now();

        switch (ev->Type()) {
#define XX(T) \
            case TEvBlobStorage::Ev##T##Result: \
                Mon->RespStat##T->Account(static_cast<TEvBlobStorage::TEv##T##Result&>(*ev).Status); \
                break;

            XX(Put)
            XX(Get)
            XX(Block)
            XX(Discover)
            XX(Range)
            XX(CollectGarbage)
            XX(Status)
            XX(Patch)
            default:
                Y_FAIL();
#undef XX
        }

        // ensure that we are dying for the first time
        Y_VERIFY(!Dead);
        if (RequestHandleClass && PoolCounters) {
            PoolCounters->GetItem(*RequestHandleClass, RequestBytes).Register(
                RequestBytes, GeneratedSubrequests, GeneratedSubrequestBytes, Timer.Passed());
        }

        const TActorId proxyId = GetProxyActorId();
        if (timeStats) {
            Derived().Send(proxyId, new TEvTimeStats(std::move(*timeStats)));
        }

        if (LatencyQueueKind) {
            Derived().Send(proxyId, new TEvLatencyReport(*LatencyQueueKind, now - RequestStartTime));
        }

        // KIKIMR-6737
        if (ev->Type() == TEvBlobStorage::EvGetResult) {
            static_cast<TEvBlobStorage::TEvGetResult&>(*ev).Sent = now;
        }

        // send the reply to original request sender
        Derived().Send(source, ev.release(), 0, cookie, std::move(traceId));
    };

    void SendResponse(std::unique_ptr<IEventBase>&& ev, TBlobStorageGroupProxyTimeStats *timeStats = nullptr) {
        SendResponse(std::move(ev), timeStats, Source, Cookie, std::move(TraceId));
    }

    static double GetStartTime(const NKikimrBlobStorage::TTimestamps& timestamps) {
        return timestamps.GetSentByDSProxyUs() / 1e6;
    }

    static double GetTotalTimeMs(const NKikimrBlobStorage::TTimestamps& timestamps) {
        return double(timestamps.GetReceivedByDSProxyUs() - timestamps.GetSentByDSProxyUs())/1000.0;
    }

    static double GetVDiskTimeMs(const NKikimrBlobStorage::TTimestamps& timestamps) {
        return double(timestamps.GetSentByVDiskUs() - timestamps.GetReceivedByVDiskUs())/1000.0;
    }

private:
    TDerived& Derived() {
        return static_cast<TDerived&>(*this);
    }

    void CheckPostponedQueue() {
        if (PostponedQ.size() == RequestsInFlight) {
            for (auto& ev : std::exchange(PostponedQ, {})) {
                TActivationContext::Send(ev.release());
            }
        }
    }

protected:
    TIntrusivePtr<TBlobStorageGroupInfo> Info;
    TIntrusivePtr<TGroupQueues> GroupQueues;
    TIntrusivePtr<TBlobStorageGroupProxyMon> Mon;
    TIntrusivePtr<TStoragePoolCounters> PoolCounters;
    TLogContext LogCtx;
    NWilson::TTraceId TraceId;
    TStackVec<std::pair<TDiskResponsivenessTracker::TDiskId, TDuration>, 16> Responsiveness;
    TString ErrorReason;
    TMaybe<TStoragePoolCounters::EHandleClass> RequestHandleClass;
    ui32 RequestBytes = 0;
    ui32 GeneratedSubrequests = 0;
    ui32 GeneratedSubrequestBytes = 0;
    bool Dead = false;
    const ui32 RestartCounter = 0;

private:
    const TActorId Source;
    const ui64 Cookie;
    std::shared_ptr<TMessageRelevanceTracker> MessageRelevanceTracker = std::make_shared<TMessageRelevanceTracker>();
    ui32 RequestsInFlight = 0;
    std::unique_ptr<IEventBase> Response;
    const TMaybe<TGroupStat::EKind> LatencyQueueKind;
    const TInstant RequestStartTime;
    THPTimer Timer;
    std::deque<std::unique_ptr<IEventHandle>> PostponedQ;
    TBlobStorageGroupInfo::TGroupFailDomains RacingDomains; // a set of domains we've received RACE from
};

void Encrypt(char *destination, const char *source, size_t shift, size_t sizeBytes, const TLogoBlobID &id,
        const TBlobStorageGroupInfo &info);
void Decrypt(char *destination, const char *source, size_t shift, size_t sizeBytes, const TLogoBlobID &id,
        const TBlobStorageGroupInfo &info);

IActor* CreateBlobStorageGroupRangeRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvRange *ev,
    ui64 cookie, NWilson::TTraceId traceId, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters);

IActor* CreateBlobStorageGroupPutRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvPut *ev,
    ui64 cookie, NWilson::TTraceId traceId, bool timeStatsEnabled,
    TDiskResponsivenessTracker::TPerDiskStatsPtr stats,
    TMaybe<TGroupStat::EKind> latencyQueueKind, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters,
    bool enableRequestMod3x3ForMinLatecy);

IActor* CreateBlobStorageGroupPutRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon,
    TBatchedVec<TEvBlobStorage::TEvPut::TPtr> &ev,
    bool timeStatsEnabled,
    TDiskResponsivenessTracker::TPerDiskStatsPtr stats,
    TMaybe<TGroupStat::EKind> latencyQueueKind, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters,
    NKikimrBlobStorage::EPutHandleClass handleClass, TEvBlobStorage::TEvPut::ETactic tactic,
    bool enableRequestMod3x3ForMinLatecy);

IActor* CreateBlobStorageGroupGetRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvGet *ev,
    ui64 cookie, NWilson::TTraceId traceId, TNodeLayoutInfoPtr&& nodeLayout,
    TMaybe<TGroupStat::EKind> latencyQueueKind, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters,
    bool isVMultiPutMode);

IActor* CreateBlobStorageGroupPatchRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvPatch *ev,
    ui64 cookie, NWilson::TTraceId traceId, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters,
    const TActorId &proxyId, bool useVPatch);

IActor* CreateBlobStorageGroupMultiGetRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvGet *ev,
    ui64 cookie, NWilson::TTraceId traceId, TMaybe<TGroupStat::EKind> latencyQueueKind,
    TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters);

IActor* CreateBlobStorageGroupIndexRestoreGetRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvGet *ev,
    ui64 cookie, NWilson::TTraceId traceId, TMaybe<TGroupStat::EKind> latencyQueueKind,
    TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters);


IActor* CreateBlobStorageGroupDiscoverRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvDiscover *ev,
    ui64 cookie, NWilson::TTraceId traceId, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters);

IActor* CreateBlobStorageGroupMirror3dcDiscoverRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvDiscover *ev,
    ui64 cookie, NWilson::TTraceId traceId, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters);

IActor* CreateBlobStorageGroupMirror3of4DiscoverRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvDiscover *ev,
    ui64 cookie, NWilson::TTraceId traceId, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters);

IActor* CreateBlobStorageGroupCollectGarbageRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvCollectGarbage *ev,
    ui64 cookie, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters);

IActor* CreateBlobStorageGroupMultiCollectRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvCollectGarbage *ev,
    ui64 cookie, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters);

IActor* CreateBlobStorageGroupBlockRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvBlock *ev,
    ui64 cookie, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters);

IActor* CreateBlobStorageGroupStatusRequest(const TIntrusivePtr<TBlobStorageGroupInfo> &info,
    const TIntrusivePtr<TGroupQueues> &state, const TActorId &source,
    const TIntrusivePtr<TBlobStorageGroupProxyMon> &mon, TEvBlobStorage::TEvStatus *ev,
    ui64 cookie, TInstant now, TIntrusivePtr<TStoragePoolCounters> &storagePoolCounters);

IActor* CreateBlobStorageGroupEjectedProxy(ui32 groupId, TIntrusivePtr<TDsProxyNodeMon> &nodeMon);

IActor* CreateBlobStorageGroupProxyConfigured(TIntrusivePtr<TBlobStorageGroupInfo>&& info,
    bool forceWaitAllDrives, TIntrusivePtr<TDsProxyNodeMon> &nodeMon,
    TIntrusivePtr<TStoragePoolCounters>&& storagePoolCounters, const TControlWrapper &enablePutBatching,
    const TControlWrapper &enableVPatch);

IActor* CreateBlobStorageGroupProxyUnconfigured(ui32 groupId, TIntrusivePtr<TDsProxyNodeMon> &nodeMon,
    const TControlWrapper &enablePutBatching, const TControlWrapper &enableVPatch);

}//NKikimr
