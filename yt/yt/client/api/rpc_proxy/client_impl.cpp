#include "client_impl.h"

#include "config.h"
#include "credentials_injecting_channel.h"
#include "helpers.h"
#include "private.h"
#include "table_mount_cache.h"
#include "timestamp_provider.h"
#include "transaction.h"

#include <yt/yt/client/api/rowset.h>
#include <yt/yt/client/api/transaction.h>

#include <yt/yt/client/chaos_client/replication_card_serialization.h>

#include <yt/yt/client/transaction_client/remote_timestamp_provider.h>

#include <yt/yt/client/scheduler/operation_id_or_alias.h>

#include <yt/yt/client/table_client/name_table.h>
#include <yt/yt/client/table_client/row_base.h>
#include <yt/yt/client/table_client/row_buffer.h>
#include <yt/yt/client/table_client/schema.h>
#include <yt/yt/client/table_client/unversioned_row.h>
#include <yt/yt/client/table_client/wire_protocol.h>

#include <yt/yt/client/tablet_client/table_mount_cache.h>

#include <yt/yt/client/ypath/rich.h>

#include <yt/yt/core/net/address.h>

#include <yt/yt/core/rpc/dynamic_channel_pool.h>
#include <yt/yt/core/rpc/retrying_channel.h>
#include <yt/yt/core/rpc/stream.h>

#include <yt/yt/core/ytree/convert.h>

#include <util/generic/cast.h>

namespace NYT::NApi::NRpcProxy {

////////////////////////////////////////////////////////////////////////////////

using NYT::ToProto;
using NYT::FromProto;

using namespace NChaosClient;
using namespace NObjectClient;
using namespace NRpc;
using namespace NScheduler;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NTransactionClient;
using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

IChannelPtr CreateCredentialsInjectingChannel(
    IChannelPtr underlying,
    const TClientOptions& options)
{
    if (options.Token) {
        return CreateTokenInjectingChannel(
            underlying,
            options.User,
            *options.Token);
    } else if (options.SessionId || options.SslSessionId) {
        return CreateCookieInjectingChannel(
            underlying,
            options.User,
            options.SessionId.value_or(TString()),
            options.SslSessionId.value_or(TString()));
    } else {
        return CreateUserInjectingChannel(underlying, options.User);
    }
}

////////////////////////////////////////////////////////////////////////////////

TClient::TClient(
    TConnectionPtr connection,
    const TClientOptions& clientOptions)
    : Connection_(std::move(connection))
    , RetryingChannel_(MaybeCreateRetryingChannel(
        CreateCredentialsInjectingChannel(
            Connection_->CreateChannel(false),
            clientOptions),
        /*retryProxyBanned*/ true))
    , ClientOptions_(clientOptions)
    , TableMountCache_(BIND(
        &CreateTableMountCache,
        Connection_->GetConfig()->TableMountCache,
        RetryingChannel_,
        RpcProxyClientLogger,
        Connection_->GetConfig()->RpcTimeout))
    , TimestampProvider_(BIND(&TClient::CreateTimestampProvider, Unretained(this)))
{ }

const ITableMountCachePtr& TClient::GetTableMountCache()
{
    return TableMountCache_.Value();
}

const IReplicationCardCachePtr& TClient::GetReplicationCardCache()
{
    YT_UNIMPLEMENTED();
}

const ITimestampProviderPtr& TClient::GetTimestampProvider()
{
    return TimestampProvider_.Value();
}

void TClient::Terminate()
{ }

////////////////////////////////////////////////////////////////////////////////

IChannelPtr TClient::MaybeCreateRetryingChannel(NRpc::IChannelPtr channel, bool retryProxyBanned) const
{
    const auto& config = Connection_->GetConfig();
    if (config->EnableRetries) {
        return NRpc::CreateRetryingChannel(
            config->RetryingChannel,
            std::move(channel),
            BIND([=] (const TError& error) {
                return IsRetriableError(error, retryProxyBanned);
            }));
    } else {
        return channel;
    }
}

IChannelPtr TClient::CreateNonRetryingChannelByAddress(const TString& address) const
{
    return CreateCredentialsInjectingChannel(
        Connection_->CreateChannelByAddress(address),
        ClientOptions_);
}

////////////////////////////////////////////////////////////////////////////////

TConnectionPtr TClient::GetRpcProxyConnection()
{
    return Connection_;
}

TClientPtr TClient::GetRpcProxyClient()
{
    return this;
}

////////////////////////////////////////////////////////////////////////////////

IChannelPtr TClient::GetRetryingChannel() const
{
    return RetryingChannel_;
}

IChannelPtr TClient::CreateNonRetryingStickyChannel() const
{
    return CreateCredentialsInjectingChannel(
        Connection_->CreateChannel(true),
        ClientOptions_);
}

IChannelPtr TClient::WrapStickyChannelIntoRetrying(IChannelPtr underlying) const
{
    return MaybeCreateRetryingChannel(
        std::move(underlying),
        /*retryProxyBanned*/ false);
}

////////////////////////////////////////////////////////////////////////////////

ITimestampProviderPtr TClient::CreateTimestampProvider() const
{
    return NRpcProxy::CreateTimestampProvider(
        RetryingChannel_,
        Connection_->GetConfig()->RpcTimeout,
        Connection_->GetConfig()->TimestampProviderLatestTimestampUpdatePeriod);
}

ITransactionPtr TClient::AttachTransaction(
    TTransactionId transactionId,
    const TTransactionAttachOptions& options)
{
    auto connection = GetRpcProxyConnection();
    auto client = GetRpcProxyClient();

    auto channel = options.StickyAddress
        ? WrapStickyChannelIntoRetrying(CreateNonRetryingChannelByAddress(options.StickyAddress))
        : GetRetryingChannel();

    auto proxy = CreateApiServiceProxy(channel);

    auto req = proxy.AttachTransaction();
    ToProto(req->mutable_transaction_id(), transactionId);
    // COMPAT(kiselyovp): remove auto_abort from the protocol
    req->set_auto_abort(false);
    if (options.PingPeriod) {
        req->set_ping_period(options.PingPeriod->GetValue());
    }
    req->set_ping(options.Ping);
    req->set_ping_ancestors(options.PingAncestors);

    auto rsp = NConcurrency::WaitFor(req->Invoke())
        .ValueOrThrow();

    auto transactionType = static_cast<ETransactionType>(rsp->type());
    auto startTimestamp = static_cast<TTimestamp>(rsp->start_timestamp());
    auto atomicity = static_cast<EAtomicity>(rsp->atomicity());
    auto durability = static_cast<EDurability>(rsp->durability());
    auto timeout = TDuration::FromValue(NYT::FromProto<i64>(rsp->timeout()));

    if (options.StickyAddress && transactionType != ETransactionType::Tablet) {
        THROW_ERROR_EXCEPTION("Sticky address is supported for tablet transactions only");
    }

    std::optional<TStickyTransactionParameters> stickyParameters;
    if (options.StickyAddress || transactionType == ETransactionType::Tablet) {
        stickyParameters.emplace();
        if (options.StickyAddress) {
            stickyParameters->ProxyAddress = options.StickyAddress;
        } else {
            stickyParameters->ProxyAddress = rsp->GetAddress();
        }
    }

    return CreateTransaction(
        std::move(connection),
        std::move(client),
        std::move(channel),
        transactionId,
        startTimestamp,
        transactionType,
        atomicity,
        durability,
        timeout,
        options.PingAncestors,
        options.PingPeriod,
        std::move(stickyParameters),
        rsp->sequence_number_source_id(),
        "Transaction attached");
}

TFuture<void> TClient::MountTable(
    const TYPath& path,
    const TMountTableOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.MountTable();
    SetTimeoutOptions(*req, options);

    req->set_path(path);

    NYT::ToProto(req->mutable_cell_id(), options.CellId);
    if (!options.TargetCellIds.empty()) {
        NYT::ToProto(req->mutable_target_cell_ids(), options.TargetCellIds);
    }
    req->set_freeze(options.Freeze);

    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_tablet_range_options(), options);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::UnmountTable(
    const TYPath& path,
    const TUnmountTableOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.UnmountTable();
    SetTimeoutOptions(*req, options);

    req->set_path(path);

    req->set_force(options.Force);

    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_tablet_range_options(), options);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::RemountTable(
    const TYPath& path,
    const TRemountTableOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.RemountTable();
    SetTimeoutOptions(*req, options);

    req->set_path(path);

    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_tablet_range_options(), options);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::FreezeTable(
    const TYPath& path,
    const TFreezeTableOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.FreezeTable();
    SetTimeoutOptions(*req, options);

    req->set_path(path);

    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_tablet_range_options(), options);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::UnfreezeTable(
    const TYPath& path,
    const TUnfreezeTableOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.UnfreezeTable();
    SetTimeoutOptions(*req, options);

    req->set_path(path);

    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_tablet_range_options(), options);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::ReshardTable(
    const TYPath& path,
    const std::vector<TLegacyOwningKey>& pivotKeys,
    const TReshardTableOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.ReshardTable();
    SetTimeoutOptions(*req, options);

    req->set_path(path);

    auto writer = CreateWireProtocolWriter();
    // XXX(sandello): This is ugly and inefficient.
    std::vector<TUnversionedRow> keys;
    for (const auto& pivotKey : pivotKeys) {
        keys.push_back(pivotKey);
    }
    writer->WriteRowset(MakeRange(keys));
    req->Attachments() = writer->Finish();

    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_tablet_range_options(), options);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::ReshardTable(
    const TYPath& path,
    int tabletCount,
    const TReshardTableOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.ReshardTable();
    SetTimeoutOptions(*req, options);

    req->set_path(path);
    req->set_tablet_count(tabletCount);
    if (options.Uniform) {
        req->set_uniform(*options.Uniform);
    }
    if (options.EnableSlicing) {
        req->set_enable_slicing(*options.EnableSlicing);
    }
    if (options.SlicingAccuracy) {
        req->set_slicing_accuracy(*options.SlicingAccuracy);
    }

    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_tablet_range_options(), options);

    return req->Invoke().As<void>();
}

TFuture<std::vector<TTabletActionId>> TClient::ReshardTableAutomatic(
    const TYPath& path,
    const TReshardTableAutomaticOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.ReshardTableAutomatic();
    SetTimeoutOptions(*req, options);

    req->set_path(path);
    req->set_keep_actions(options.KeepActions);

    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_tablet_range_options(), options);

    return req->Invoke().Apply(BIND([] (const TErrorOr<TApiServiceProxy::TRspReshardTableAutomaticPtr>& rspOrError) {
        const auto& rsp = rspOrError.ValueOrThrow();
        return FromProto<std::vector<TTabletActionId>>(rsp->tablet_actions());
    }));
}

TFuture<void> TClient::TrimTable(
    const TYPath& path,
    int tabletIndex,
    i64 trimmedRowCount,
    const TTrimTableOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.TrimTable();
    SetTimeoutOptions(*req, options);

    req->set_path(path);
    req->set_tablet_index(tabletIndex);
    req->set_trimmed_row_count(trimmedRowCount);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::AlterTable(
    const TYPath& path,
    const TAlterTableOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.AlterTable();
    SetTimeoutOptions(*req, options);

    req->set_path(path);

    if (options.Schema) {
        req->set_schema(ConvertToYsonString(*options.Schema).ToString());
    }
    if (options.Dynamic) {
        req->set_dynamic(*options.Dynamic);
    }
    if (options.UpstreamReplicaId) {
        ToProto(req->mutable_upstream_replica_id(), *options.UpstreamReplicaId);
    }
    if (options.SchemaModification) {
        req->set_schema_modification(static_cast<NProto::ETableSchemaModification>(*options.SchemaModification));
    }
    if (options.ReplicationProgress) {
        ToProto(req->mutable_replication_progress(), *options.ReplicationProgress);
    }

    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_transactional_options(), options);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::AlterTableReplica(
    TTableReplicaId replicaId,
    const TAlterTableReplicaOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.AlterTableReplica();
    SetTimeoutOptions(*req, options);

    ToProto(req->mutable_replica_id(), replicaId);

    if (options.Enabled) {
        req->set_enabled(*options.Enabled);
    }
    if (options.Mode) {
        req->set_mode(static_cast<NProto::ETableReplicaMode>(*options.Mode));
    }
    if (options.PreserveTimestamps) {
        req->set_preserve_timestamps(*options.PreserveTimestamps);
    }
    if (options.Atomicity) {
        req->set_atomicity(static_cast<NProto::EAtomicity>(*options.Atomicity));
    }

    ToProto(req->mutable_mutating_options(), options);

    return req->Invoke().As<void>();
}

TFuture<TYsonString> TClient::GetTablePivotKeys(
    const TYPath& path,
    const TGetTablePivotKeysOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetTablePivotKeys();
    SetTimeoutOptions(*req, options);

    req->set_path(path);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspGetTablePivotKeysPtr& rsp) {
        return TYsonString(rsp->value());
    }));
}

TFuture<void> TClient::CreateTableBackup(
    const TBackupManifestPtr& /*manifest*/,
    const TCreateTableBackupOptions& /*options*/)
{
    ThrowUnimplemented("CreateTableBackup");
}

TFuture<void> TClient::RestoreTableBackup(
    const TBackupManifestPtr& /*manifest*/,
    const TRestoreTableBackupOptions& /*options*/)
{
    ThrowUnimplemented("RestoreTableBackup");
}

TFuture<std::vector<TTableReplicaId>> TClient::GetInSyncReplicas(
    const TYPath& path,
    const TNameTablePtr& nameTable,
    const TSharedRange<TLegacyKey>& keys,
    const TGetInSyncReplicasOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetInSyncReplicas();
    SetTimeoutOptions(*req, options);

    if (options.Timestamp) {
        req->set_timestamp(options.Timestamp);
    }

    req->set_path(path);
    req->Attachments() = SerializeRowset(nameTable, keys, req->mutable_rowset_descriptor());

    return req->Invoke().Apply(BIND([] (const TErrorOr<TApiServiceProxy::TRspGetInSyncReplicasPtr>& rspOrError) {
        const auto& rsp = rspOrError.ValueOrThrow();
        return FromProto<std::vector<TTableReplicaId>>(rsp->replica_ids());
    }));
}

TFuture<std::vector<TTableReplicaId>> TClient::GetInSyncReplicas(
    const TYPath& path,
    const TGetInSyncReplicasOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetInSyncReplicas();
    SetTimeoutOptions(*req, options);

    if (options.Timestamp) {
        req->set_timestamp(options.Timestamp);
    }

    req->set_path(path);
    req->RequireServerFeature(ERpcProxyFeature::GetInSyncWithoutKeys);

    return req->Invoke().Apply(BIND([] (const TErrorOr<TApiServiceProxy::TRspGetInSyncReplicasPtr>& rspOrError) {
        const auto& rsp = rspOrError.ValueOrThrow();
        return FromProto<std::vector<TTableReplicaId>>(rsp->replica_ids());
    }));
}

TFuture<std::vector<TTabletInfo>> TClient::GetTabletInfos(
    const TYPath& path,
    const std::vector<int>& tabletIndexes,
    const TGetTabletInfosOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetTabletInfos();
    SetTimeoutOptions(*req, options);

    req->set_path(path);
    ToProto(req->mutable_tablet_indexes(), tabletIndexes);
    req->set_request_errors(options.RequestErrors);

    return req->Invoke().Apply(BIND([] (const TErrorOr<TApiServiceProxy::TRspGetTabletInfosPtr>& rspOrError) {
        const auto& rsp = rspOrError.ValueOrThrow();
        std::vector<TTabletInfo> tabletInfos;
        tabletInfos.reserve(rsp->tablets_size());
        for (const auto& protoTabletInfo : rsp->tablets()) {
            auto& tabletInfo = tabletInfos.emplace_back();
            tabletInfo.TotalRowCount = protoTabletInfo.total_row_count();
            tabletInfo.TrimmedRowCount = protoTabletInfo.trimmed_row_count();
            tabletInfo.DelayedLocklessRowCount = protoTabletInfo.delayed_lockless_row_count();
            tabletInfo.BarrierTimestamp = protoTabletInfo.barrier_timestamp();
            tabletInfo.LastWriteTimestamp = protoTabletInfo.last_write_timestamp();
            tabletInfo.TableReplicaInfos = protoTabletInfo.replicas().empty()
                ? std::nullopt
                : std::make_optional(std::vector<TTabletInfo::TTableReplicaInfo>());
            FromProto(&tabletInfo.TabletErrors, protoTabletInfo.tablet_errors());

            for (const auto& protoReplicaInfo : protoTabletInfo.replicas()) {
                auto& currentReplica = tabletInfo.TableReplicaInfos->emplace_back();
                currentReplica.ReplicaId = FromProto<TGuid>(protoReplicaInfo.replica_id());
                currentReplica.LastReplicationTimestamp = protoReplicaInfo.last_replication_timestamp();
                currentReplica.Mode = CheckedEnumCast<ETableReplicaMode>(protoReplicaInfo.mode());
                currentReplica.CurrentReplicationRowIndex = protoReplicaInfo.current_replication_row_index();
                currentReplica.CommittedReplicationRowIndex = protoReplicaInfo.committed_replication_row_index();
                currentReplica.ReplicationError = FromProto<TError>(protoReplicaInfo.replication_error());
            }
        }
        return tabletInfos;
    }));
}

TFuture<TGetTabletErrorsResult> TClient::GetTabletErrors(
    const TYPath& path,
    const TGetTabletErrorsOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetTabletErrors();
    SetTimeoutOptions(*req, options);

    req->set_path(path);
    if (options.Limit) {
        req->set_limit(*options.Limit);
    }

    return req->Invoke().Apply(BIND([] (const TErrorOr<TApiServiceProxy::TRspGetTabletErrorsPtr>& rspOrError) {
        const auto& rsp = rspOrError.ValueOrThrow();
        TGetTabletErrorsResult tabletErrors;

        for (i64 index = 0; index != rsp->tablet_ids_size(); ++index) {
            std::vector<TError> errors;
            for (const auto& protoError : rsp->tablet_errors(index).errors()) {
                errors.push_back(FromProto<TError>(protoError));
            }
            tabletErrors.TabletErrors[FromProto<TTabletId>(rsp->tablet_ids(index))] = std::move(errors);
        }

        for (i64 index = 0; index != rsp->replica_ids_size(); ++index) {
            std::vector<TError> errors;
            for (const auto& protoError : rsp->replication_errors(index).errors()) {
                errors.push_back(FromProto<TError>(protoError));
            }
            tabletErrors.ReplicationErrors[FromProto<TTableReplicaId>(rsp->replica_ids(index))] = std::move(errors);
        }
        return tabletErrors;
    }));
}

TFuture<std::vector<TTabletActionId>> TClient::BalanceTabletCells(
    const TString& tabletCellBundle,
    const std::vector<NYPath::TYPath>& movableTables,
    const TBalanceTabletCellsOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.BalanceTabletCells();
    SetTimeoutOptions(*req, options);

    req->set_bundle(tabletCellBundle);
    req->set_keep_actions(options.KeepActions);
    ToProto(req->mutable_movable_tables(), movableTables);

    return req->Invoke().Apply(BIND([] (const TErrorOr<TApiServiceProxy::TRspBalanceTabletCellsPtr>& rspOrError) {
        const auto& rsp = rspOrError.ValueOrThrow();
        return FromProto<std::vector<TTabletActionId>>(rsp->tablet_actions());
    }));
}

TFuture<NChaosClient::TReplicationCardPtr> TClient::GetReplicationCard(
    NChaosClient::TReplicationCardId /*replicationCardId*/,
    const TGetReplicationCardOptions& /*options*/)
{
    YT_UNIMPLEMENTED();
}

TFuture<void> TClient::UpdateChaosTableReplicaProgress(
    NChaosClient::TReplicaId /*replicaId*/,
    const TUpdateChaosTableReplicaProgressOptions& /*options*/)
{
    YT_UNIMPLEMENTED();
}

TFuture<void> TClient::AddMember(
    const TString& group,
    const TString& member,
    const TAddMemberOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.AddMember();
    SetTimeoutOptions(*req, options);

    req->set_group(group);
    req->set_member(member);
    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_prerequisite_options(), options);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::RemoveMember(
    const TString& group,
    const TString& member,
    const TRemoveMemberOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.RemoveMember();
    SetTimeoutOptions(*req, options);

    req->set_group(group);
    req->set_member(member);
    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_prerequisite_options(), options);

    return req->Invoke().As<void>();
}

TFuture<TCheckPermissionResponse> TClient::CheckPermission(
    const TString& user,
    const NYPath::TYPath& path,
    EPermission permission,
    const TCheckPermissionOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.CheckPermission();
    SetTimeoutOptions(*req, options);

    req->set_user(user);
    req->set_path(path);
    req->set_permission(static_cast<int>(permission));
    if (options.Columns) {
        auto* protoColumns = req->mutable_columns();
        ToProto(protoColumns->mutable_items(), *options.Columns);
    }
    if (options.Vital) {
        req->set_vital(*options.Vital);
    }

    ToProto(req->mutable_master_read_options(), options);
    ToProto(req->mutable_transactional_options(), options);
    ToProto(req->mutable_prerequisite_options(), options);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspCheckPermissionPtr& rsp) {
        TCheckPermissionResponse response;
        static_cast<TCheckPermissionResult&>(response) = FromProto<TCheckPermissionResult>(rsp->result());
        if (rsp->has_columns()) {
            response.Columns = FromProto<std::vector<TCheckPermissionResult>>(rsp->columns().items());
        }
        return response;
    }));
}

TFuture<TCheckPermissionByAclResult> TClient::CheckPermissionByAcl(
    const std::optional<TString>& user,
    EPermission permission,
    INodePtr acl,
    const TCheckPermissionByAclOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.CheckPermissionByAcl();
    SetTimeoutOptions(*req, options);

    if (user) {
        req->set_user(*user);
    }
    req->set_permission(static_cast<int>(permission));
    req->set_acl(ConvertToYsonString(acl).ToString());
    req->set_ignore_missing_subjects(options.IgnoreMissingSubjects);

    ToProto(req->mutable_master_read_options(), options);
    ToProto(req->mutable_prerequisite_options(), options);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspCheckPermissionByAclPtr& rsp) {
        return FromProto<TCheckPermissionByAclResult>(rsp->result());
    }));
}

TFuture<void> TClient::TransferAccountResources(
    const TString& srcAccount,
    const TString& dstAccount,
    NYTree::INodePtr resourceDelta,
    const TTransferAccountResourcesOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.TransferAccountResources();
    SetTimeoutOptions(*req, options);

    req->set_src_account(srcAccount);
    req->set_dst_account(dstAccount);
    req->set_resource_delta(ConvertToYsonString(resourceDelta).ToString());

    ToProto(req->mutable_mutating_options(), options);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::TransferPoolResources(
    const TString& srcPool,
    const TString& dstPool,
    const TString& poolTree,
    NYTree::INodePtr resourceDelta,
    const TTransferPoolResourcesOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.TransferPoolResources();
    SetTimeoutOptions(*req, options);

    req->set_src_pool(srcPool);
    req->set_dst_pool(dstPool);
    req->set_pool_tree(poolTree);
    req->set_resource_delta(ConvertToYsonString(resourceDelta).ToString());

    ToProto(req->mutable_mutating_options(), options);

    return req->Invoke().As<void>();
}

TFuture<NScheduler::TOperationId> TClient::StartOperation(
    NScheduler::EOperationType type,
    const TYsonString& spec,
    const TStartOperationOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.StartOperation();
    SetTimeoutOptions(*req, options);

    req->set_type(NProto::ConvertOperationTypeToProto(type));
    req->set_spec(spec.ToString());

    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_transactional_options(), options);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspStartOperationPtr& rsp) {
        return FromProto<TOperationId>(rsp->operation_id());
    }));
}

TFuture<void> TClient::AbortOperation(
    const TOperationIdOrAlias& operationIdOrAlias,
    const TAbortOperationOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.AbortOperation();
    SetTimeoutOptions(*req, options);

    NScheduler::ToProto(req, operationIdOrAlias);

    if (options.AbortMessage) {
        req->set_abort_message(*options.AbortMessage);
    }

    return req->Invoke().As<void>();
}

TFuture<void> TClient::SuspendOperation(
    const TOperationIdOrAlias& operationIdOrAlias,
    const TSuspendOperationOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.SuspendOperation();
    SetTimeoutOptions(*req, options);

    NScheduler::ToProto(req, operationIdOrAlias);
    req->set_abort_running_jobs(options.AbortRunningJobs);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::ResumeOperation(
    const TOperationIdOrAlias& operationIdOrAlias,
    const TResumeOperationOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.ResumeOperation();
    SetTimeoutOptions(*req, options);

    NScheduler::ToProto(req, operationIdOrAlias);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::CompleteOperation(
    const TOperationIdOrAlias& operationIdOrAlias,
    const TCompleteOperationOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.CompleteOperation();
    SetTimeoutOptions(*req, options);

    NScheduler::ToProto(req, operationIdOrAlias);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::UpdateOperationParameters(
    const TOperationIdOrAlias& operationIdOrAlias,
    const TYsonString& parameters,
    const TUpdateOperationParametersOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.UpdateOperationParameters();
    SetTimeoutOptions(*req, options);

    NScheduler::ToProto(req, operationIdOrAlias);

    req->set_parameters(parameters.ToString());

    return req->Invoke().As<void>();
}

TFuture<TOperation> TClient::GetOperation(
    const TOperationIdOrAlias& operationIdOrAlias,
    const TGetOperationOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetOperation();
    SetTimeoutOptions(*req, options);

    NScheduler::ToProto(req, operationIdOrAlias);

    ToProto(req->mutable_master_read_options(), options);
    if (options.Attributes) {
        NYT::ToProto(req->mutable_attributes(), *options.Attributes);
    }
    req->set_include_runtime(options.IncludeRuntime);
    req->set_maximum_cypress_progress_age(ToProto<i64>(options.MaximumCypressProgressAge));

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspGetOperationPtr& rsp) {
        auto attributes = ConvertToAttributes(TYsonStringBuf(rsp->meta()));
        TOperation operation;
        Deserialize(operation, std::move(attributes), /*clone*/ false);
        return operation;
    }));
}

TFuture<void> TClient::DumpJobContext(
    NJobTrackerClient::TJobId jobId,
    const NYPath::TYPath& path,
    const TDumpJobContextOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.DumpJobContext();
    SetTimeoutOptions(*req, options);

    ToProto(req->mutable_job_id(), jobId);
    req->set_path(path);

    return req->Invoke().As<void>();
}

TFuture<NConcurrency::IAsyncZeroCopyInputStreamPtr> TClient::GetJobInput(
    NJobTrackerClient::TJobId jobId,
    const TGetJobInputOptions& options)
{
    auto proxy = CreateApiServiceProxy();
    auto req = proxy.GetJobInput();
    if (options.Timeout) {
        SetTimeoutOptions(*req, options);
    } else {
        InitStreamingRequest(*req);
    }

    ToProto(req->mutable_job_id(), jobId);

    return CreateRpcClientInputStream(std::move(req));
}

TFuture<TYsonString> TClient::GetJobInputPaths(
    NJobTrackerClient::TJobId jobId,
    const TGetJobInputPathsOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetJobInputPaths();
    SetTimeoutOptions(*req, options);

    ToProto(req->mutable_job_id(), jobId);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspGetJobInputPathsPtr& rsp) {
        return TYsonString(rsp->paths());
    }));
}

TFuture<TYsonString> TClient::GetJobSpec(
    NJobTrackerClient::TJobId jobId,
    const TGetJobSpecOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetJobSpec();
    SetTimeoutOptions(*req, options);

    ToProto(req->mutable_job_id(), jobId);
    req->set_omit_node_directory(options.OmitNodeDirectory);
    req->set_omit_input_table_specs(options.OmitInputTableSpecs);
    req->set_omit_output_table_specs(options.OmitOutputTableSpecs);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspGetJobSpecPtr& rsp) {
        return TYsonString(rsp->job_spec());
    }));
}

TFuture<TSharedRef> TClient::GetJobStderr(
    const TOperationIdOrAlias& operationIdOrAlias,
    NJobTrackerClient::TJobId jobId,
    const TGetJobStderrOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetJobStderr();
    SetTimeoutOptions(*req, options);

    NScheduler::ToProto(req, operationIdOrAlias);
    ToProto(req->mutable_job_id(), jobId);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspGetJobStderrPtr& rsp) {
        YT_VERIFY(rsp->Attachments().size() == 1);
        return rsp->Attachments().front();
    }));
}

TFuture<TSharedRef> TClient::GetJobFailContext(
    const TOperationIdOrAlias& operationIdOrAlias,
    NJobTrackerClient::TJobId jobId,
    const TGetJobFailContextOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetJobFailContext();
    SetTimeoutOptions(*req, options);

    NScheduler::ToProto(req, operationIdOrAlias);
    ToProto(req->mutable_job_id(), jobId);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspGetJobFailContextPtr& rsp) {
        YT_VERIFY(rsp->Attachments().size() == 1);
        return rsp->Attachments().front();
    }));
}

TFuture<TListOperationsResult> TClient::ListOperations(
    const TListOperationsOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.ListOperations();
    SetTimeoutOptions(*req, options);

    if (options.FromTime) {
        req->set_from_time(NYT::ToProto<i64>(*options.FromTime));
    }
    if (options.ToTime) {
        req->set_to_time(NYT::ToProto<i64>(*options.ToTime));
    }
    if (options.CursorTime) {
        req->set_cursor_time(NYT::ToProto<i64>(*options.CursorTime));
    }
    req->set_cursor_direction(static_cast<NProto::EOperationSortDirection>(options.CursorDirection));
    if (options.UserFilter) {
        req->set_user_filter(*options.UserFilter);
    }

    if (options.AccessFilter) {
        req->set_access_filter(ConvertToYsonString(options.AccessFilter).ToString());
    }

    if (options.StateFilter) {
        req->set_state_filter(NProto::ConvertOperationStateToProto(*options.StateFilter));
    }
    if (options.TypeFilter) {
        req->set_type_filter(NProto::ConvertOperationTypeToProto(*options.TypeFilter));
    }
    if (options.SubstrFilter) {
        req->set_substr_filter(*options.SubstrFilter);
    }
    if (options.Pool) {
        req->set_pool(*options.Pool);
    }
    if (options.PoolTree) {
        req->set_pool_tree(*options.PoolTree);
    }
    if (options.WithFailedJobs) {
        req->set_with_failed_jobs(*options.WithFailedJobs);
    }
    if (options.ArchiveFetchingTimeout) {
        req->set_archive_fetching_timeout(NYT::ToProto<i64>(options.ArchiveFetchingTimeout));
    }
    req->set_include_archive(options.IncludeArchive);
    req->set_include_counters(options.IncludeCounters);
    req->set_limit(options.Limit);

    ToProto(req->mutable_attributes(), options.Attributes);

    req->set_enable_ui_mode(options.EnableUIMode);

    ToProto(req->mutable_master_read_options(), options);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspListOperationsPtr& rsp) {
        return FromProto<TListOperationsResult>(rsp->result());
    }));
}

TFuture<TListJobsResult> TClient::ListJobs(
    const TOperationIdOrAlias& operationIdOrAlias,
    const TListJobsOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.ListJobs();
    SetTimeoutOptions(*req, options);

    NScheduler::ToProto(req, operationIdOrAlias);

    if (options.Type) {
        req->set_type(NProto::ConvertJobTypeToProto(*options.Type));
    }
    if (options.State) {
        req->set_state(NProto::ConvertJobStateToProto(*options.State));
    }
    if (options.Address) {
        req->set_address(*options.Address);
    }
    if (options.WithStderr) {
        req->set_with_stderr(*options.WithStderr);
    }
    if (options.WithFailContext) {
        req->set_with_fail_context(*options.WithFailContext);
    }
    if (options.WithSpec) {
        req->set_with_spec(*options.WithSpec);
    }
    if (options.JobCompetitionId) {
        ToProto(req->mutable_job_competition_id(), options.JobCompetitionId);
    }
    if (options.WithCompetitors) {
        req->set_with_competitors(*options.WithCompetitors);
    }
    if (options.TaskName) {
        req->set_task_name(*options.TaskName);
    }

    req->set_sort_field(static_cast<NProto::EJobSortField>(options.SortField));
    req->set_sort_order(static_cast<NProto::EJobSortDirection>(options.SortOrder));

    req->set_limit(options.Limit);
    req->set_offset(options.Offset);

    req->set_include_cypress(options.IncludeCypress);
    req->set_include_controller_agent(options.IncludeControllerAgent);
    req->set_include_archive(options.IncludeArchive);

    req->set_data_source(static_cast<NProto::EDataSource>(options.DataSource));
    req->set_running_jobs_lookbehind_period(NYT::ToProto<i64>(options.RunningJobsLookbehindPeriod));

    ToProto(req->mutable_master_read_options(), options);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspListJobsPtr& rsp) {
        return FromProto<TListJobsResult>(rsp->result());
    }));
}

TFuture<TYsonString> TClient::GetJob(
    const TOperationIdOrAlias& operationIdOrAlias,
    NJobTrackerClient::TJobId jobId,
    const TGetJobOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetJob();
    SetTimeoutOptions(*req, options);

    NScheduler::ToProto(req, operationIdOrAlias);
    ToProto(req->mutable_job_id(), jobId);

    ToProto(req->mutable_attributes(), options.Attributes);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspGetJobPtr& rsp) {
        return TYsonString(rsp->info());
    }));
}

TFuture<void> TClient::AbandonJob(
    NJobTrackerClient::TJobId jobId,
    const TAbandonJobOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.AbandonJob();
    SetTimeoutOptions(*req, options);

    ToProto(req->mutable_job_id(), jobId);

    return req->Invoke().As<void>();
}

TFuture<TPollJobShellResponse> TClient::PollJobShell(
    NJobTrackerClient::TJobId jobId,
    const std::optional<TString>& shellName,
    const TYsonString& parameters,
    const TPollJobShellOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.PollJobShell();
    SetTimeoutOptions(*req, options);

    ToProto(req->mutable_job_id(), jobId);
    req->set_parameters(parameters.ToString());
    if (shellName) {
        req->set_shell_name(*shellName);
    }

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspPollJobShellPtr& rsp) {
        return TPollJobShellResponse {
            .Result = TYsonString(rsp->result()),
        };
    }));
}

TFuture<void> TClient::AbortJob(
    NJobTrackerClient::TJobId jobId,
    const TAbortJobOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.AbortJob();
    SetTimeoutOptions(*req, options);

    ToProto(req->mutable_job_id(), jobId);
    if (options.InterruptTimeout) {
        req->set_interrupt_timeout(NYT::ToProto<i64>(*options.InterruptTimeout));
    }

    return req->Invoke().As<void>();
}

TFuture<TGetFileFromCacheResult> TClient::GetFileFromCache(
    const TString& md5,
    const TGetFileFromCacheOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetFileFromCache();
    SetTimeoutOptions(*req, options);
    ToProto(req->mutable_transactional_options(), options);

    req->set_md5(md5);
    req->set_cache_path(options.CachePath);

    ToProto(req->mutable_master_read_options(), options);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspGetFileFromCachePtr& rsp) {
        return FromProto<TGetFileFromCacheResult>(rsp->result());
    }));
}

TFuture<TPutFileToCacheResult> TClient::PutFileToCache(
    const NYPath::TYPath& path,
    const TString& expectedMD5,
    const TPutFileToCacheOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.PutFileToCache();
    SetTimeoutOptions(*req, options);
    ToProto(req->mutable_transactional_options(), options);

    req->set_path(path);
    req->set_md5(expectedMD5);
    req->set_cache_path(options.CachePath);
    req->set_preserve_expiration_timeout(options.PreserveExpirationTimeout);

    ToProto(req->mutable_prerequisite_options(), options);
    ToProto(req->mutable_master_read_options(), options);
    ToProto(req->mutable_mutating_options(), options);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspPutFileToCachePtr& rsp) {
        return FromProto<TPutFileToCacheResult>(rsp->result());
    }));
}

TFuture<TClusterMeta> TClient::GetClusterMeta(
    const TGetClusterMetaOptions& /*options*/)
{
    ThrowUnimplemented("GetClusterMeta");
}

TFuture<void> TClient::CheckClusterLiveness(
    const TCheckClusterLivenessOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.CheckClusterLiveness();
    SetTimeoutOptions(*req, options);

    req->set_check_cypress_root(options.CheckCypressRoot);
    req->set_check_secondary_master_cells(options.CheckSecondaryMasterCells);

    return req->Invoke().As<void>();
}

TFuture<TSkynetSharePartsLocationsPtr> TClient::LocateSkynetShare(
    const NYPath::TRichYPath&,
    const TLocateSkynetShareOptions& /*options*/)
{
    ThrowUnimplemented("LocateSkynetShare");
}

TFuture<std::vector<TColumnarStatistics>> TClient::GetColumnarStatistics(
    const std::vector<NYPath::TRichYPath>& path,
    const TGetColumnarStatisticsOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GetColumnarStatistics();
    SetTimeoutOptions(*req, options);

    for (const auto& subPath: path) {
        req->add_paths(ConvertToYsonString(subPath).ToString());
    }

    req->set_fetcher_mode(static_cast<NProto::EColumnarStatisticsFetcherMode>(options.FetcherMode));

    req->mutable_fetch_chunk_spec()->set_max_chunk_per_fetch(
        options.FetchChunkSpecConfig->MaxChunksPerFetch);
    req->mutable_fetch_chunk_spec()->set_max_chunk_per_locate_request(
        options.FetchChunkSpecConfig->MaxChunksPerLocateRequest);

    req->mutable_fetcher()->set_node_rpc_timeout(
        NYT::ToProto<i64>(options.FetcherConfig->NodeRpcTimeout));

    req->set_enable_early_finish(options.EnableEarlyFinish);

    ToProto(req->mutable_transactional_options(), options);

    return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspGetColumnarStatisticsPtr& rsp) {
        return NYT::FromProto<std::vector<TColumnarStatistics>>(rsp->statistics());
    }));
}

TFuture<void> TClient::TruncateJournal(
    const NYPath::TYPath& path,
    i64 rowCount,
    const TTruncateJournalOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.TruncateJournal();
    SetTimeoutOptions(*req, options);

    req->set_path(path);
    req->set_row_count(rowCount);
    ToProto(req->mutable_mutating_options(), options);
    ToProto(req->mutable_prerequisite_options(), options);

    return req->Invoke().As<void>();
}


TFuture<int> TClient::BuildSnapshot(const TBuildSnapshotOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.BuildSnapshot();
    if (options.CellId) {
        ToProto(req->mutable_cell_id(), options.CellId);
    }
    req->set_set_read_only(options.SetReadOnly);
    req->set_wait_for_snapshot_completion(options.WaitForSnapshotCompletion);

    return req->Invoke().Apply(BIND([] (const TErrorOr<TApiServiceProxy::TRspBuildSnapshotPtr>& rspOrError) -> int {
        const auto& rsp = rspOrError.ValueOrThrow();
        return rsp->snapshot_id();
    }));
}

TFuture<TCellIdToSnapshotIdMap> TClient::BuildMasterSnapshots(const TBuildMasterSnapshotsOptions& /*options*/)
{
    ThrowUnimplemented("BuildMasterSnapshots");
}

TFuture<void> TClient::SwitchLeader(
    NHydra::TCellId /*cellId*/,
    const TString& /*newLeaderAddress*/,
    const TSwitchLeaderOptions& /*options*/)
{
    ThrowUnimplemented("SwitchLeader");
}

TFuture<void> TClient::GCCollect(const TGCCollectOptions& options)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.GCCollect();
    if (options.CellId) {
        ToProto(req->mutable_cell_id(), options.CellId);
    }

    return req->Invoke().As<void>();
}

TFuture<void> TClient::KillProcess(
    const TString& /*address*/,
    const TKillProcessOptions& /*options*/)
{
    ThrowUnimplemented("KillProcess");
}

TFuture<TString> TClient::WriteCoreDump(
    const TString& /*address*/,
    const TWriteCoreDumpOptions& /*options*/)
{
    ThrowUnimplemented("WriteCoreDump");
}

TFuture<TGuid> TClient::WriteLogBarrier(
    const TString& /*address*/,
    const TWriteLogBarrierOptions& /*options*/)
{
    ThrowUnimplemented("WriteLogBarrier");
}

TFuture<TString> TClient::WriteOperationControllerCoreDump(
    TOperationId /*operationId*/,
    const TWriteOperationControllerCoreDumpOptions& /*options*/)
{
    ThrowUnimplemented("WriteOperationControllerCoreDump");
}

TFuture<void> TClient::HealExecNode(
    const TString& /*address*/,
    const THealExecNodeOptions& /*options*/)
{
    ThrowUnimplemented("HealExecNode");
}

TFuture<void> TClient::SuspendCoordinator(
    TCellId coordinatorCellId,
    const TSuspendCoordinatorOptions& /*options*/)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.SuspendCoordinator();
    ToProto(req->mutable_coordinator_cell_id(), coordinatorCellId);

    return req->Invoke().As<void>();
}

TFuture<void> TClient::ResumeCoordinator(
    TCellId coordinatorCellId,
    const TResumeCoordinatorOptions& /*options*/)
{
    auto proxy = CreateApiServiceProxy();

    auto req = proxy.ResumeCoordinator();
    ToProto(req->mutable_coordinator_cell_id(), coordinatorCellId);

    return req->Invoke().As<void>();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpcProxy
