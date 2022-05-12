#pragma once

#include <yt/yt/client/object_client/public.h>

#include <yt/yt/client/table_client/public.h>

#include <yt/yt/client/transaction_client/public.h>

#include <yt/yt/core/misc/public.h>

namespace NYT::NApi {

////////////////////////////////////////////////////////////////////////////////

using TClusterTag = NObjectClient::TCellTag;

////////////////////////////////////////////////////////////////////////////////

// Keep in sync with NRpcProxy::NProto::EMasterReadKind.
// On cache miss request is redirected to next level cache:
// Local cache -> (node) cache -> master cache
DEFINE_ENUM(EMasterChannelKind,
    ((Leader)                (0))
    ((Follower)              (1))
    // Use local (per-connection) cache.
    ((LocalCache)            (4))
    // Use cache located on nodes.
    ((Cache)                 (2))
    // Use cache located on masters (if caching on masters is enabled).
    ((MasterCache)           (3))
);

DEFINE_ENUM(EUserWorkloadCategory,
    (Batch)
    (Interactive)
    (Realtime)
);

YT_DEFINE_ERROR_ENUM(
    ((TooManyConcurrentRequests)                         (1900))
    ((JobArchiveUnavailable)                             (1910))
    ((RetriableArchiveError)                             (1911))
    ((NoSuchOperation)                                   (1915))
    ((NoSuchJob)                                         (1916))
    ((UncertainOperationControllerState)                 (1917))
    ((NoSuchAttribute)                                   (1920))
    ((FormatDisabled)                                    (1925))
);

DEFINE_ENUM(ERowModificationType,
    ((Write)            (0))
    ((Delete)           (1))
    ((VersionedWrite)   (2))
    ((WriteAndLock)     (3))
);

DEFINE_ENUM(EReplicaConsistency,
    ((None)             (0))
    ((Sync)             (1))
);

DEFINE_ENUM(ETransactionCoordinatorCommitMode,
    // Success is reported when phase 2 starts (all participants have prepared but not yet committed).
    ((Eager)  (0))
    // Success is reported when transaction is finished (all participants have committed).
    ((Lazy)   (1))
);

DEFINE_ENUM(EProxyType,
    ((Http) (1))
    ((Rpc)  (2))
    ((Grpc) (3))
);

////////////////////////////////////////////////////////////////////////////////

template <class TRow>
struct IRowset;
template <class TRow>
using IRowsetPtr = TIntrusivePtr<IRowset<TRow>>;

using IUnversionedRowset = IRowset<NTableClient::TUnversionedRow>;
using IVersionedRowset = IRowset<NTableClient::TVersionedRow>;

DECLARE_REFCOUNTED_TYPE(IUnversionedRowset)
DECLARE_REFCOUNTED_TYPE(IVersionedRowset)
DECLARE_REFCOUNTED_STRUCT(IPersistentQueueRowset)
DECLARE_REFCOUNTED_STRUCT(TSkynetSharePartsLocations);

struct TConnectionOptions;

struct TClientOptions;
struct TTransactionParticipantOptions;

struct TTimeoutOptions;
struct TTransactionalOptions;
struct TPrerequisiteOptions;
struct TMasterReadOptions;
struct TMutatingOptions;
struct TSuppressableAccessTrackingOptions;
struct TTabletRangeOptions;

struct TGetFileFromCacheResult;
struct TPutFileToCacheResult;

DECLARE_REFCOUNTED_STRUCT(IConnection)
DECLARE_REFCOUNTED_STRUCT(IClientBase)
DECLARE_REFCOUNTED_STRUCT(IClient)
DECLARE_REFCOUNTED_STRUCT(ITransaction)
DECLARE_REFCOUNTED_STRUCT(IStickyTransactionPool)

DECLARE_REFCOUNTED_STRUCT(ITableReader)
DECLARE_REFCOUNTED_STRUCT(ITableWriter)

DECLARE_REFCOUNTED_STRUCT(IFileReader)
DECLARE_REFCOUNTED_STRUCT(IFileWriter)

DECLARE_REFCOUNTED_STRUCT(IJournalReader)
DECLARE_REFCOUNTED_STRUCT(IJournalWriter)

DECLARE_REFCOUNTED_CLASS(TPersistentQueuePoller)

DECLARE_REFCOUNTED_CLASS(TTableMountCacheConfig)
DECLARE_REFCOUNTED_CLASS(TConnectionConfig)
DECLARE_REFCOUNTED_CLASS(TConnectionDynamicConfig)
DECLARE_REFCOUNTED_CLASS(TPersistentQueuePollerConfig)

DECLARE_REFCOUNTED_CLASS(TFileReaderConfig)
DECLARE_REFCOUNTED_CLASS(TFileWriterConfig)
DECLARE_REFCOUNTED_CLASS(TJournalReaderConfig)
DECLARE_REFCOUNTED_CLASS(TJournalWriterConfig)

DECLARE_REFCOUNTED_STRUCT(TPrerequisiteRevisionConfig)

DECLARE_REFCOUNTED_STRUCT(TDetailedProfilingInfo)

DECLARE_REFCOUNTED_STRUCT(TSchedulingOptions)

DECLARE_REFCOUNTED_CLASS(TJobInputReader)

DECLARE_REFCOUNTED_CLASS(TClientCache)

DECLARE_REFCOUNTED_STRUCT(TTableBackupManifest)
DECLARE_REFCOUNTED_STRUCT(TBackupManifest)

////////////////////////////////////////////////////////////////////////////////

extern const TString RpcProxiesPath;
extern const TString GrpcProxiesPath;
extern const TString DefaultProxyRole;
extern const TString BannedAttributeName;
extern const TString RoleAttributeName;
extern const TString AliveNodeName;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi

