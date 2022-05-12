#include "service_yndx_keyvalue.h"

#include <kikimr/yndx/api/protos/ydb_yndx_keyvalue.pb.h>

#include <ydb/core/base/path.h>
#include <ydb/core/grpc_services/rpc_scheme_base.h>
#include <ydb/core/grpc_services/rpc_common.h>
#include <ydb/core/keyvalue/keyvalue_events.h>
#include <ydb/core/tx/scheme_cache/scheme_cache.h>
#include <ydb/core/mind/local.h>
#include <ydb/core/protos/local.pb.h>

#include <kikimr/yndx/api/protos/ydb_yndx_keyvalue.pb.h>

namespace NKikimr::NGRpcService {

using namespace NActors;
using namespace Ydb;

using TEvCreateVolumeYndxKeyValueRequest =
    TGrpcRequestOperationCall<Ydb::Yndx::KeyValue::CreateVolumeRequest,
        Ydb::Yndx::KeyValue::CreateVolumeResponse>;
using TEvDropVolumeYndxKeyValueRequest =
    TGrpcRequestOperationCall<Ydb::Yndx::KeyValue::DropVolumeRequest,
        Ydb::Yndx::KeyValue::DropVolumeResponse>;
using TEvListLocalPartitionsYndxKeyValueRequest =
    TGrpcRequestOperationCall<Ydb::Yndx::KeyValue::ListLocalPartitionsRequest,
        Ydb::Yndx::KeyValue::ListLocalPartitionsResponse>;

using TEvAcquireLockYndxKeyValueRequest =
    TGrpcRequestOperationCall<Ydb::Yndx::KeyValue::AcquireLockRequest,
        Ydb::Yndx::KeyValue::AcquireLockResponse>;
using TEvExecuteTransactionYndxKeyValueRequest =
    TGrpcRequestOperationCall<Ydb::Yndx::KeyValue::ExecuteTransactionRequest,
        Ydb::Yndx::KeyValue::ExecuteTransactionResponse>;
using TEvReadYndxKeyValueRequest =
    TGrpcRequestOperationCall<Ydb::Yndx::KeyValue::ReadRequest,
        Ydb::Yndx::KeyValue::ReadResponse>;
using TEvReadRangeYndxKeyValueRequest =
    TGrpcRequestOperationCall<Ydb::Yndx::KeyValue::ReadRangeRequest,
        Ydb::Yndx::KeyValue::ReadRangeResponse>;
using TEvListRangeYndxKeyValueRequest =
    TGrpcRequestOperationCall<Ydb::Yndx::KeyValue::ListRangeRequest,
        Ydb::Yndx::KeyValue::ListRangeResponse>;
using TEvGetStorageChannelStatusYndxKeyValueRequest =
    TGrpcRequestOperationCall<Ydb::Yndx::KeyValue::GetStorageChannelStatusRequest,
        Ydb::Yndx::KeyValue::GetStorageChannelStatusResponse>;

} // namespace NKikimr::NGRpcService


namespace NKikimr::NGRpcService {

using namespace NActors;
using namespace Ydb;

#define COPY_PRIMITIVE_FIELD(name) \
    to->set_ ## name(static_cast<decltype(to->name())>(from.name())) \
// COPY_PRIMITIVE_FIELD

#define COPY_PRIMITIVE_OPTIONAL_FIELD(name) \
    if (from.has_ ## name()) { \
        to->set_ ## name(static_cast<decltype(to->name())>(from.name())); \
    } \
// COPY_PRIMITIVE_FIELD

namespace {

void CopyProtobuf(const Ydb::Yndx::KeyValue::AcquireLockRequest &/*from*/,
        NKikimrKeyValue::AcquireLockRequest */*to*/)
{
}

void CopyProtobuf(const NKikimrKeyValue::AcquireLockResult &from,
        Ydb::Yndx::KeyValue::AcquireLockResult *to)
{
    COPY_PRIMITIVE_FIELD(lock_generation);
}


void CopyProtobuf(const Ydb::Yndx::KeyValue::ExecuteTransactionRequest::Command::Rename &from,
        NKikimrKeyValue::ExecuteTransactionRequest::Command::Rename *to)
{
    COPY_PRIMITIVE_FIELD(old_key);
    COPY_PRIMITIVE_FIELD(new_key);
}

void CopyProtobuf(const Ydb::Yndx::KeyValue::ExecuteTransactionRequest::Command::Concat &from,
        NKikimrKeyValue::ExecuteTransactionRequest::Command::Concat *to)
{
    *to->mutable_input_keys() = from.input_keys();
    COPY_PRIMITIVE_FIELD(output_key);
    COPY_PRIMITIVE_FIELD(keep_inputs);
}

void CopyProtobuf(const Ydb::Yndx::KeyValue::KeyRange &from, NKikimrKeyValue::KVRange *to) {
#define CHECK_AND_SET(name) \
    if (from.has_ ## name()) { \
        COPY_PRIMITIVE_FIELD(name); \
    } \
// CHECK_AND_SET

    CHECK_AND_SET(from_key_inclusive)
    CHECK_AND_SET(from_key_exclusive)
    CHECK_AND_SET(to_key_inclusive)
    CHECK_AND_SET(to_key_exclusive)

#undef CHECK_AND_SET
}

void CopyProtobuf(const Ydb::Yndx::KeyValue::ExecuteTransactionRequest::Command::CopyRange &from,
        NKikimrKeyValue::ExecuteTransactionRequest::Command::CopyRange *to)
{
    CopyProtobuf(from.range(), to->mutable_range());
    COPY_PRIMITIVE_FIELD(prefix_to_remove);
    COPY_PRIMITIVE_FIELD(prefix_to_add);
}

template <typename TProtoFrom, typename TProtoTo>
void CopyPriority(TProtoFrom &&from, TProtoTo *to) {
    switch(from.priority()) {
    case Ydb::Yndx::KeyValue::Priorities::PRIORITY_REALTIME:
        to->set_priority(NKikimrKeyValue::Priorities::PRIORITY_REALTIME);
        break;
    case Ydb::Yndx::KeyValue::Priorities::PRIORITY_BACKGROUND:
        to->set_priority(NKikimrKeyValue::Priorities::PRIORITY_BACKGROUND);
        break;
    default:
        to->set_priority(NKikimrKeyValue::Priorities::PRIORITY_UNSPECIFIED);
        break;
    }
}

void CopyProtobuf(const Ydb::Yndx::KeyValue::ExecuteTransactionRequest::Command::Write &from,
        NKikimrKeyValue::ExecuteTransactionRequest::Command::Write *to)
{
    COPY_PRIMITIVE_FIELD(key);
    COPY_PRIMITIVE_FIELD(value);
    COPY_PRIMITIVE_FIELD(storage_channel);
    CopyPriority(from, to);
    switch(from.tactic()) {
    case Ydb::Yndx::KeyValue::ExecuteTransactionRequest::Command::Write::TACTIC_MAX_THROUGHPUT:
        to->set_tactic(NKikimrKeyValue::ExecuteTransactionRequest::Command::Write::TACTIC_MAX_THROUGHPUT);
        break;
    case Ydb::Yndx::KeyValue::ExecuteTransactionRequest::Command::Write::TACTIC_MIN_LATENCY:
        to->set_tactic(NKikimrKeyValue::ExecuteTransactionRequest::Command::Write::TACTIC_MIN_LATENCY);
        break;
    default:
        to->set_tactic(NKikimrKeyValue::ExecuteTransactionRequest::Command::Write::TACTIC_UNSPECIFIED);
        break;
    }
}

void CopyProtobuf(const Ydb::Yndx::KeyValue::ExecuteTransactionRequest::Command::DeleteRange &from,
        NKikimrKeyValue::ExecuteTransactionRequest::Command::DeleteRange *to)
{
    CopyProtobuf(from.range(), to->mutable_range());
}

void CopyProtobuf(const Ydb::Yndx::KeyValue::ExecuteTransactionRequest::Command &from,
        NKikimrKeyValue::ExecuteTransactionRequest::Command *to)
{
#define CHECK_AND_COPY(name) \
    if (from.has_ ## name()) { \
        CopyProtobuf(from.name(), to->mutable_ ## name()); \
    } \
// CHECK_AND_COPY

    CHECK_AND_COPY(rename)
    CHECK_AND_COPY(concat)
    CHECK_AND_COPY(copy_range)
    CHECK_AND_COPY(write)
    CHECK_AND_COPY(delete_range)

#undef CHECK_AND_COPY
}

void CopyProtobuf(const Ydb::Yndx::KeyValue::ExecuteTransactionRequest &from,
        NKikimrKeyValue::ExecuteTransactionRequest *to)
{
    COPY_PRIMITIVE_OPTIONAL_FIELD(lock_generation);
    for (auto &cmd : from.commands()) {
        CopyProtobuf(cmd, to->add_commands());
    }
}

void CopyProtobuf(const NKikimrKeyValue::StorageChannel &from, Ydb::Yndx::KeyValue::StorageChannelInfo *to) {
    COPY_PRIMITIVE_FIELD(storage_channel);
    COPY_PRIMITIVE_FIELD(status_flag);
}

void CopyProtobuf(const NKikimrKeyValue::ExecuteTransactionResult &from,
        Ydb::Yndx::KeyValue::ExecuteTransactionResult *to)
{
    for (auto &channel : from.storage_channel()) {
        CopyProtobuf(channel, to->add_storage_channel_info());
    }
}

void CopyProtobuf(const Ydb::Yndx::KeyValue::ReadRequest &from, NKikimrKeyValue::ReadRequest *to) {
    COPY_PRIMITIVE_OPTIONAL_FIELD(lock_generation);
    COPY_PRIMITIVE_FIELD(key);
    COPY_PRIMITIVE_FIELD(offset);
    COPY_PRIMITIVE_FIELD(size);
    CopyPriority(from, to);
    COPY_PRIMITIVE_FIELD(limit_bytes);
}

void CopyProtobuf(const NKikimrKeyValue::ReadResult &from, Ydb::Yndx::KeyValue::ReadResult *to) {
    COPY_PRIMITIVE_FIELD(requested_key);
    COPY_PRIMITIVE_FIELD(requested_offset);
    COPY_PRIMITIVE_FIELD(requested_size);
    COPY_PRIMITIVE_FIELD(value);
    switch (from.status()) {
        case NKikimrKeyValue::Statuses::RSTATUS_OVERRUN:
            to->set_is_overrun(true);
            break;
        default:
            break;
    }
}

void CopyProtobuf(const Ydb::Yndx::KeyValue::ReadRangeRequest &from, NKikimrKeyValue::ReadRangeRequest *to) {
    COPY_PRIMITIVE_OPTIONAL_FIELD(lock_generation);
    CopyProtobuf(from.range(), to->mutable_range());
    to->set_include_data(true);
    COPY_PRIMITIVE_FIELD(limit_bytes);
    CopyPriority(from, to);
}

void CopyProtobuf(const Ydb::Yndx::KeyValue::ListRangeRequest &from, NKikimrKeyValue::ReadRangeRequest *to) {
    COPY_PRIMITIVE_OPTIONAL_FIELD(lock_generation);
    CopyProtobuf(from.range(), to->mutable_range());
    to->set_include_data(false);
    COPY_PRIMITIVE_FIELD(limit_bytes);
}

void CopyProtobuf(const NKikimrKeyValue::ReadRangeResult::KeyValuePair &from,
        Ydb::Yndx::KeyValue::ReadRangeResult::KeyValuePair *to)
{
    COPY_PRIMITIVE_FIELD(key);
    COPY_PRIMITIVE_FIELD(value);
    COPY_PRIMITIVE_FIELD(creation_unix_time);
    COPY_PRIMITIVE_FIELD(storage_channel);
}

void CopyProtobuf(const NKikimrKeyValue::ReadRangeResult &from,
        Ydb::Yndx::KeyValue::ReadRangeResult *to)
{
    for (auto &pair : from.pair()) {
        CopyProtobuf(pair, to->add_pair());
    }
    if (from.status() == NKikimrKeyValue::Statuses::RSTATUS_OVERRUN) {
        to->set_is_overrun(true);
    }
}

void CopyProtobuf(const NKikimrKeyValue::ReadRangeResult::KeyValuePair &from,
        Ydb::Yndx::KeyValue::ListRangeResult::KeyInfo *to)
{
    COPY_PRIMITIVE_FIELD(key);
    COPY_PRIMITIVE_FIELD(value_size);
    COPY_PRIMITIVE_FIELD(creation_unix_time);
    COPY_PRIMITIVE_FIELD(storage_channel);
}

void CopyProtobuf(const NKikimrKeyValue::ReadRangeResult &from,
        Ydb::Yndx::KeyValue::ListRangeResult *to)
{
    for (auto &pair : from.pair()) {
        CopyProtobuf(pair, to->add_key());
    }
    if (from.status() == NKikimrKeyValue::Statuses::RSTATUS_OVERRUN) {
        to->set_is_overrun(true);
    }
}

void CopyProtobuf(const Ydb::Yndx::KeyValue::GetStorageChannelStatusRequest &from,
        NKikimrKeyValue::GetStorageChannelStatusRequest *to)
{
    COPY_PRIMITIVE_OPTIONAL_FIELD(lock_generation);
    *to->mutable_storage_channel() = from.storage_channel();
}


void CopyProtobuf(const NKikimrKeyValue::GetStorageChannelStatusResult &from,
        Ydb::Yndx::KeyValue::GetStorageChannelStatusResult *to)
{
    for (auto &channel : from.storage_channel()) {
        CopyProtobuf(channel, to->add_storage_channel_info());
    }
}


Ydb::StatusIds::StatusCode PullStatus(const NKikimrKeyValue::AcquireLockResult &) {
    return Ydb::StatusIds::SUCCESS;
}

template <typename TResult>
Ydb::StatusIds::StatusCode PullStatus(const TResult &result) {
    switch (result.status()) {
    case NKikimrKeyValue::Statuses::RSTATUS_OK:
    case NKikimrKeyValue::Statuses::RSTATUS_OVERRUN:
        return Ydb::StatusIds::SUCCESS;
    case NKikimrKeyValue::Statuses::RSTATUS_ERROR:
        return Ydb::StatusIds::BAD_REQUEST;
    case NKikimrKeyValue::Statuses::RSTATUS_TIMEOUT:
        return Ydb::StatusIds::TIMEOUT;
    case NKikimrKeyValue::Statuses::RSTATUS_NOT_FOUND:
        return Ydb::StatusIds::NOT_FOUND;
    case NKikimrKeyValue::Statuses::RSTATUS_WRONG_LOCK_GENERATION:
        return Ydb::StatusIds::PRECONDITION_FAILED;
    default:
        return Ydb::StatusIds::INTERNAL_ERROR;
    }
}


class TCreateVolumeRequest : public TRpcSchemeRequestActor<TCreateVolumeRequest, TEvCreateVolumeYndxKeyValueRequest> {
public:
    using TBase = TRpcSchemeRequestActor<TCreateVolumeRequest, TEvCreateVolumeYndxKeyValueRequest>;
    using TBase::TBase;

    void Bootstrap(const TActorContext& ctx) {
        TBase::Bootstrap(ctx);
        Become(&TCreateVolumeRequest::StateFunc);
        SendProposeRequest(ctx);
    }

    void SendProposeRequest(const TActorContext &ctx) {
        const auto req = this->GetProtoRequest();

        std::pair<TString, TString> pathPair;
        try {
            pathPair = SplitPath(Request_->GetDatabaseName(), req->path());
        } catch (const std::exception& ex) {
            Request_->RaiseIssue(NYql::ExceptionToIssue(ex));
            return Reply(StatusIds::BAD_REQUEST, ctx);
        }
        const auto& workingDir = pathPair.first;
        const auto& name = pathPair.second;

        std::unique_ptr<TEvTxUserProxy::TEvProposeTransaction> proposeRequest = this->CreateProposeTransaction();
        NKikimrTxUserProxy::TEvProposeTransaction& record = proposeRequest->Record;
        NKikimrSchemeOp::TModifyScheme* modifyScheme = record.MutableTransaction()->MutableModifyScheme();
        modifyScheme->SetWorkingDir(workingDir);
        NKikimrSchemeOp::TCreateSolomonVolume* tableDesc = nullptr;

        modifyScheme->SetOperationType(NKikimrSchemeOp::EOperationType::ESchemeOpCreateSolomonVolume);
        tableDesc = modifyScheme->MutableCreateSolomonVolume();
        tableDesc->SetName(name);
        tableDesc->SetChannelProfileId(req->storage_channel_profile_id());
        tableDesc->SetPartitionCount(req->partition_count());

        ctx.Send(MakeTxProxyID(), proposeRequest.release());
    }

    STFUNC(StateFunc) {
        return TBase::StateWork(ev, ctx);
    }
};


class TDropVolumeRequest : public TRpcSchemeRequestActor<TDropVolumeRequest, TEvDropVolumeYndxKeyValueRequest> {
public:
    using TBase = TRpcSchemeRequestActor<TDropVolumeRequest, TEvDropVolumeYndxKeyValueRequest>;
    using TBase::TBase;

    void Bootstrap(const TActorContext& ctx) {
        TBase::Bootstrap(ctx);
        Become(&TCreateVolumeRequest::StateFunc);
        SendProposeRequest(ctx);
    }

    void SendProposeRequest(const TActorContext &ctx) {
        const auto req = this->GetProtoRequest();

        std::pair<TString, TString> pathPair;
        try {
            pathPair = SplitPath(req->path());
        } catch (const std::exception& ex) {
            Request_->RaiseIssue(NYql::ExceptionToIssue(ex));
            return Reply(StatusIds::BAD_REQUEST, ctx);
        }
        const auto& workingDir = pathPair.first;
        const auto& name = pathPair.second;

        std::unique_ptr<TEvTxUserProxy::TEvProposeTransaction> proposeRequest = this->CreateProposeTransaction();
        NKikimrTxUserProxy::TEvProposeTransaction& record = proposeRequest->Record;
        NKikimrSchemeOp::TModifyScheme* modifyScheme = record.MutableTransaction()->MutableModifyScheme();
        modifyScheme->SetWorkingDir(workingDir);
        NKikimrSchemeOp::TDrop* drop = nullptr;

        modifyScheme->SetOperationType(NKikimrSchemeOp::EOperationType::ESchemeOpDropSolomonVolume);
        drop = modifyScheme->MutableDrop();
        drop->SetName(name);

        ctx.Send(MakeTxProxyID(), proposeRequest.release());
    }

    STFUNC(StateFunc) {
        return TBase::StateWork(ev, ctx);
    }
};


class TListLocalPartitionsRequest
        : public TRpcOperationRequestActor<TListLocalPartitionsRequest, TEvListLocalPartitionsYndxKeyValueRequest>
{
public:
    using TBase = TRpcOperationRequestActor<TListLocalPartitionsRequest, TEvListLocalPartitionsYndxKeyValueRequest>;
    using TBase::TBase;

    void Bootstrap(const TActorContext& ctx) {
        TBase::Bootstrap(ctx);
        this->Become(&TListLocalPartitionsRequest::StateFunc);
        Ydb::StatusIds::StatusCode status = Ydb::StatusIds::STATUS_CODE_UNSPECIFIED;
        NYql::TIssues issues;
        if (!ValidateRequest(status, issues)) {
            this->Reply(status, issues, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }
        SendNavigateRequest();
    }

protected:
    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvTxProxySchemeCache::TEvNavigateKeySetResult, Handle);
            hFunc(TEvLocal::TEvEnumerateTabletsResult, Handle);
        default:
            return TBase::StateFuncBase(ev, ctx);
        }
    }

    void SendNavigateRequest() {
        auto &rec = *this->GetProtoRequest();
        auto req = MakeHolder<NSchemeCache::TSchemeCacheNavigate>();
        auto& entry = req->ResultSet.emplace_back();
        entry.Path = ::NKikimr::SplitPath(rec.path());
        entry.RequestType = NSchemeCache::TSchemeCacheNavigate::TEntry::ERequestType::ByPath;
        entry.ShowPrivatePath = true;
        entry.SyncVersion = false;
        req->UserToken = new NACLib::TUserToken(this->Request_->GetInternalToken());
        req->DatabaseName = Request_->GetDatabaseName().GetOrElse("");
        auto ev = new TEvTxProxySchemeCache::TEvNavigateKeySet(req.Release());
        this->Send(MakeSchemeCacheID(), ev);
    }

    void Handle(TEvTxProxySchemeCache::TEvNavigateKeySetResult::TPtr &ev) {
        TEvTxProxySchemeCache::TEvNavigateKeySetResult* res = ev->Get();

        if (res->Request->ResultSet.size() != 1) {
            this->Reply(StatusIds::INTERNAL_ERROR, "Received an incorrect answer from SchemeCache.", NKikimrIssues::TIssuesIds::UNEXPECTED, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (res->Request->ResultSet[0].Status == NSchemeCache::TSchemeCacheNavigate::EStatus::PathNotPath) {
            this->Reply(StatusIds::BAD_REQUEST, "Incorrect path.", NKikimrIssues::TIssuesIds::PATH_NOT_EXIST, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (res->Request->ResultSet[0].Status == NSchemeCache::TSchemeCacheNavigate::EStatus::PathNotTable) {
            this->Reply(StatusIds::BAD_REQUEST, "Incorrect path.", NKikimrIssues::TIssuesIds::DEFAULT_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (res->Request->ResultSet[0].Status == NSchemeCache::TSchemeCacheNavigate::EStatus::TableCreationNotComplete) {
            this->Reply(StatusIds::BAD_REQUEST, "Table creation isn't complete.", NKikimrIssues::TIssuesIds::DEFAULT_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (false && res->Request->ResultSet[0].Status != NSchemeCache::TSchemeCacheNavigate::EStatus::Ok &&
                res->Request->ResultSet[0].Status != NSchemeCache::TSchemeCacheNavigate::EStatus::Unknown)
        {
            this->Reply(StatusIds::INTERNAL_ERROR, "Received an incorrect answer from SchemeCache.", NKikimrIssues::TIssuesIds::UNEXPECTED, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (!res->Request->ResultSet[0].SolomonVolumeInfo) {
            this->Reply(StatusIds::BAD_REQUEST, "Table isn't keyvalue.", NKikimrIssues::TIssuesIds::DEFAULT_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        const NKikimrSchemeOp::TSolomonVolumeDescription &desc = res->Request->ResultSet[0].SolomonVolumeInfo->Description;
        for (const NKikimrSchemeOp::TSolomonVolumeDescription::TPartition &partition : desc.GetPartitions()) {
            TabletIdToPartitionId[partition.GetTabletId()] = partition.GetPartitionId();
        }

        if (TabletIdToPartitionId.empty()) {
            Ydb::Yndx::KeyValue::ListLocalPartitionsResult result;
            result.set_path(this->GetProtoRequest()->path());
            result.set_node_id(SelfId().NodeId());
            this->ReplyWithResult(Ydb::StatusIds::SUCCESS, result, TActivationContext::AsActorContext());
            return;
        }

        SendRequest();
    }

    TActorId MakeLocalRegistrarID() {
        auto &ctx = TActivationContext::AsActorContext();
        auto &domainsInfo = *AppData(ctx)->DomainsInfo;
        auto domainIt = domainsInfo.Domains.find(1);
        if (domainIt == domainsInfo.Domains.end()) {
            TActorId invalidId;
            return invalidId;
        }
        auto &rec = *this->GetProtoRequest();
        ui32 nodeId = rec.node_id() ? rec.node_id() : ctx.SelfID.NodeId();
        ui32 hiveUid = domainsInfo.GetDefaultHiveUid(1);
        ui64 hiveId = domainsInfo.GetHive(hiveUid);
        return ::NKikimr::MakeLocalRegistrarID(nodeId, hiveId);
    }

    TEvLocal::TEvEnumerateTablets* MakeRequest() {
        return new TEvLocal::TEvEnumerateTablets(TTabletTypes::KeyValue);
    }

    void SendRequest() {
        Send(MakeLocalRegistrarID(), MakeRequest(), IEventHandle::FlagTrackDelivery, 0);
    }

    void Handle(TEvLocal::TEvEnumerateTabletsResult::TPtr &ev) {
        const NKikimrLocal::TEvEnumerateTabletsResult &record = ev->Get()->Record;
        if (!record.HasStatus() || record.GetStatus() != NKikimrProto::OK) {
            this->Reply(StatusIds::INTERNAL_ERROR, "Received an incorrect answer from Local.", NKikimrIssues::TIssuesIds::UNEXPECTED, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        Ydb::Yndx::KeyValue::ListLocalPartitionsResult result;
        result.set_path(this->GetProtoRequest()->path());
        result.set_node_id(SelfId().NodeId());
        for (auto &item : record.GetTabletInfo()) {
            if (!item.HasTabletId()) {
                continue;
            }
            auto it = TabletIdToPartitionId.find(item.GetTabletId());
            if (it != TabletIdToPartitionId.end()) {
                result.add_partition_ids(it->second);
            }
        }
        this->ReplyWithResult(Ydb::StatusIds::SUCCESS, result, TActivationContext::AsActorContext());
    }

    bool ValidateRequest(Ydb::StatusIds::StatusCode& /*status*/, NYql::TIssues& /*issues*/) {
        return true;
    }

private:
    THashMap<ui64, ui64> TabletIdToPartitionId;
};


template <typename TDerived, typename TRequest, typename TResultRecord, typename TKVRequest>
class TKeyValueRequestGrpc : public TRpcOperationRequestActor<TDerived, TRequest> {
public:
    using TBase = TRpcOperationRequestActor<TDerived, TRequest>;
    using TBase::TBase;

    void Bootstrap(const TActorContext& ctx) {
        TBase::Bootstrap(ctx);
        this->Become(&TKeyValueRequestGrpc::StateFunc);
        Ydb::StatusIds::StatusCode status = Ydb::StatusIds::STATUS_CODE_UNSPECIFIED;
        NYql::TIssues issues;
        if (!ValidateRequest(status, issues)) {
            this->Reply(status, issues, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }
        SendNavigateRequest();
    }


protected:
    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvTabletPipe::TEvClientConnected, Handle);
            hFunc(TEvTabletPipe::TEvClientDestroyed, Handle);
            hFunc(TKVRequest::TResponse, Handle);
            hFunc(TEvTxProxySchemeCache::TEvNavigateKeySetResult, Handle);
        default:
            return TBase::StateFuncBase(ev, ctx);
        }
    }

    void SendNavigateRequest() {
        auto &rec = *this->GetProtoRequest();
        auto req = MakeHolder<NSchemeCache::TSchemeCacheNavigate>();
        auto& entry = req->ResultSet.emplace_back();
        entry.Path = ::NKikimr::SplitPath(rec.path());
        entry.RequestType = NSchemeCache::TSchemeCacheNavigate::TEntry::ERequestType::ByPath;
        entry.ShowPrivatePath = true;
        entry.SyncVersion = false;
        req->UserToken = new NACLib::TUserToken(this->Request_->GetInternalToken());
        req->DatabaseName = this->Request_->GetDatabaseName().GetOrElse("");
        auto ev = new TEvTxProxySchemeCache::TEvNavigateKeySet(req.Release());
        this->Send(MakeSchemeCacheID(), ev);
    }

    void Handle(TEvTxProxySchemeCache::TEvNavigateKeySetResult::TPtr &ev) {
        TEvTxProxySchemeCache::TEvNavigateKeySetResult* res = ev->Get();

        if (res->Request->ResultSet.size() != 1) {
            this->Reply(StatusIds::INTERNAL_ERROR, "Received an incorrect answer from SchemeCache.", NKikimrIssues::TIssuesIds::UNEXPECTED, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (res->Request->ResultSet[0].Status == NSchemeCache::TSchemeCacheNavigate::EStatus::PathNotPath) {
            this->Reply(StatusIds::BAD_REQUEST, "Incorrect path.", NKikimrIssues::TIssuesIds::PATH_NOT_EXIST, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (res->Request->ResultSet[0].Status == NSchemeCache::TSchemeCacheNavigate::EStatus::PathNotTable) {
            this->Reply(StatusIds::BAD_REQUEST, "Incorrect path.", NKikimrIssues::TIssuesIds::DEFAULT_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (res->Request->ResultSet[0].Status == NSchemeCache::TSchemeCacheNavigate::EStatus::TableCreationNotComplete) {
            this->Reply(StatusIds::BAD_REQUEST, "Table creation isn't complete.", NKikimrIssues::TIssuesIds::DEFAULT_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (false && res->Request->ResultSet[0].Status != NSchemeCache::TSchemeCacheNavigate::EStatus::Ok &&
                res->Request->ResultSet[0].Status != NSchemeCache::TSchemeCacheNavigate::EStatus::Unknown)
        {
            this->Reply(StatusIds::INTERNAL_ERROR, "Received an incorrect answer from SchemeCache.", NKikimrIssues::TIssuesIds::UNEXPECTED, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (!res->Request->ResultSet[0].SolomonVolumeInfo) {
            this->Reply(StatusIds::BAD_REQUEST, "Table isn't keyvalue.", NKikimrIssues::TIssuesIds::DEFAULT_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        auto &rec = *this->GetProtoRequest();
        const NKikimrSchemeOp::TSolomonVolumeDescription &desc = res->Request->ResultSet[0].SolomonVolumeInfo->Description;

        if (rec.partition_id() >= desc.PartitionsSize()) {
            this->Reply(StatusIds::BAD_REQUEST, "Partition wasn't found.", NKikimrIssues::TIssuesIds::DEFAULT_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        ui64 partitionId = rec.partition_id();
        if (const auto &partition = desc.GetPartitions(rec.partition_id()); partition.GetPartitionId() == partitionId) {
            KVTabletId = partition.GetTabletId();
        } else {
            Y_VERIFY_DEBUG(false);
            for (const NKikimrSchemeOp::TSolomonVolumeDescription::TPartition &partition : desc.GetPartitions()) {
                if (partition.GetPartitionId() == partitionId)  {
                    KVTabletId = partition.GetTabletId();
                    break;
                }
            }
        }

        if (!KVTabletId) {
            this->Reply(StatusIds::BAD_REQUEST, "Partition wasn't found.", NKikimrIssues::TIssuesIds::DEFAULT_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        CreatePipe();
        SendRequest();
    }

    void SendRequest() {
        std::unique_ptr<TKVRequest> req = std::make_unique<TKVRequest>();
        auto &rec = *this->GetProtoRequest();
        CopyProtobuf(rec, &req->Record);
        req->Record.set_tablet_id(KVTabletId);
        NTabletPipe::SendData(this->SelfId(), KVPipeClient, req.release(), 0);
    }

    void Handle(typename TKVRequest::TResponse::TPtr &ev) {
        TResultRecord result;
        CopyProtobuf(ev->Get()->Record, &result);
        auto status = PullStatus(ev->Get()->Record);
        this->ReplyWithResult(status, result, TActivationContext::AsActorContext());
    }

    NTabletPipe::TClientConfig GetPipeConfig() {
        NTabletPipe::TClientConfig cfg;
        cfg.RetryPolicy = {
            .RetryLimitCount = 3u
        };
        return cfg;
    }

    void CreatePipe() {
        KVPipeClient = this->Register(NTabletPipe::CreateClient(this->SelfId(), KVTabletId, GetPipeConfig()));
    }

    void Handle(TEvTabletPipe::TEvClientConnected::TPtr& ev) {
        if (ev->Get()->Status != NKikimrProto::OK) {
            this->Reply(StatusIds::UNAVAILABLE, "Failed to connect to coordination node.", NKikimrIssues::TIssuesIds::SHARD_NOT_AVAILABLE, TActivationContext::ActorContextFor(this->SelfId()));
        }
    }

    void Handle(TEvTabletPipe::TEvClientDestroyed::TPtr&) {
        this->Reply(StatusIds::UNAVAILABLE, "Connection to coordination node was lost.", NKikimrIssues::TIssuesIds::SHARD_NOT_AVAILABLE, TActivationContext::ActorContextFor(this->SelfId()));
    }

    virtual bool ValidateRequest(Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) = 0;

    void PassAway() override {
        if (KVPipeClient) {
            NTabletPipe::CloseClient(this->SelfId(), KVPipeClient);
            KVPipeClient = {};
        }
        TBase::PassAway();
    }
protected:
    ui64 KVTabletId = 0;
    TActorId KVPipeClient;
};

class TAcquireLockRequest
    : public TKeyValueRequestGrpc<TAcquireLockRequest, TEvAcquireLockYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::AcquireLockResult, TEvKeyValue::TEvAcquireLock>
{
public:
    using TBase = TKeyValueRequestGrpc<TAcquireLockRequest, TEvAcquireLockYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::AcquireLockResult, TEvKeyValue::TEvAcquireLock>;
    using TBase::TBase;

    bool ValidateRequest(Ydb::StatusIds::StatusCode& /*status*/, NYql::TIssues& /*issues*/) override {
        return true;
    }
};


class TExecuteTransactionRequest
    : public TKeyValueRequestGrpc<TExecuteTransactionRequest, TEvExecuteTransactionYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::ExecuteTransactionResult, TEvKeyValue::TEvExecuteTransaction> {
public:
    using TBase = TKeyValueRequestGrpc<TExecuteTransactionRequest, TEvExecuteTransactionYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::ExecuteTransactionResult, TEvKeyValue::TEvExecuteTransaction>;
    using TBase::TBase;

    bool ValidateRequest(Ydb::StatusIds::StatusCode& /*status*/, NYql::TIssues& /*issues*/) override {
        return true;
    }
};

class TReadRequest
    : public TKeyValueRequestGrpc<TReadRequest, TEvReadYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::ReadResult, TEvKeyValue::TEvRead> {
public:
    using TBase = TKeyValueRequestGrpc<TReadRequest, TEvReadYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::ReadResult, TEvKeyValue::TEvRead>;
    using TBase::TBase;
    using TBase::Handle;
    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
        default:
            return TBase::StateFunc(ev, ctx);
        }
    }
    bool ValidateRequest(Ydb::StatusIds::StatusCode& /*status*/, NYql::TIssues& /*issues*/) override {
        return true;
    }
};

class TReadRangeRequest
    : public TKeyValueRequestGrpc<TReadRangeRequest, TEvReadRangeYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::ReadRangeResult, TEvKeyValue::TEvReadRange> {
public:
    using TBase = TKeyValueRequestGrpc<TReadRangeRequest, TEvReadRangeYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::ReadRangeResult, TEvKeyValue::TEvReadRange>;
    using TBase::TBase;
    using TBase::Handle;
    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
        default:
            return TBase::StateFunc(ev, ctx);
        }
    }
    bool ValidateRequest(Ydb::StatusIds::StatusCode& /*status*/, NYql::TIssues& /*issues*/) override {
        return true;
    }
};

class TListRangeRequest
    : public TKeyValueRequestGrpc<TListRangeRequest, TEvListRangeYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::ListRangeResult, TEvKeyValue::TEvReadRange> {
public:
    using TBase = TKeyValueRequestGrpc<TListRangeRequest, TEvListRangeYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::ListRangeResult, TEvKeyValue::TEvReadRange>;
    using TBase::TBase;
    using TBase::Handle;
    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
        default:
            return TBase::StateFunc(ev, ctx);
        }
    }
    bool ValidateRequest(Ydb::StatusIds::StatusCode& /*status*/, NYql::TIssues& /*issues*/) override {
        return true;
    }
};

class TGetStorageChannelStatusRequest
    : public TKeyValueRequestGrpc<TGetStorageChannelStatusRequest, TEvGetStorageChannelStatusYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::GetStorageChannelStatusResult, TEvKeyValue::TEvGetStorageChannelStatus> {
public:
    using TBase = TKeyValueRequestGrpc<TGetStorageChannelStatusRequest, TEvGetStorageChannelStatusYndxKeyValueRequest,
            Ydb::Yndx::KeyValue::GetStorageChannelStatusResult, TEvKeyValue::TEvGetStorageChannelStatus>;
    using TBase::TBase;
    using TBase::Handle;
    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
        default:
            return TBase::StateFunc(ev, ctx);
        }
    }
    bool ValidateRequest(Ydb::StatusIds::StatusCode& /*status*/, NYql::TIssues& /*issues*/) override {
        return true;
    }
};

}


void DoCreateVolumeYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TCreateVolumeRequest(p.release()));
}

void DoDropVolumeYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TDropVolumeRequest(p.release()));
}

void DoListLocalPartitionsYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TListLocalPartitionsRequest(p.release()));
}

void DoAcquireLockYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TAcquireLockRequest(p.release()));
}

void DoExecuteTransactionYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TExecuteTransactionRequest(p.release()));
}

void DoReadYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TReadRequest(p.release()));
}

void DoReadRangeYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TReadRangeRequest(p.release()));
}

void DoListRangeYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TListRangeRequest(p.release()));
}

void DoGetStorageChannelStatusYndxKeyValue(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TGetStorageChannelStatusRequest(p.release()));
}

} // namespace NKikimr::NGRpcService
