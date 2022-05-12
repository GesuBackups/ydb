#include "helpers.h"

#include <yt/yt/core/misc/guid.h>

namespace NYT::NObjectClient {

////////////////////////////////////////////////////////////////////////////////

const TStringBuf ObjectIdPathPrefix("#");

NYPath::TYPath FromObjectId(TObjectId id)
{
    return TString(ObjectIdPathPrefix) + ToString(id);
}

bool IsVersionedType(EObjectType type)
{
    return
        type == EObjectType::StringNode ||
        type == EObjectType::Int64Node ||
        type == EObjectType::Uint64Node ||
        type == EObjectType::DoubleNode ||
        type == EObjectType::BooleanNode ||
        type == EObjectType::MapNode ||
        type == EObjectType::ListNode ||
        type == EObjectType::File ||
        type == EObjectType::Table ||
        type == EObjectType::ReplicatedTable ||
        type == EObjectType::ReplicationLogTable ||
        type == EObjectType::Journal ||
        type == EObjectType::ChunkMap ||
        type == EObjectType::LostChunkMap ||
        type == EObjectType::PrecariousChunkMap ||
        type == EObjectType::OverreplicatedChunkMap ||
        type == EObjectType::UnderreplicatedChunkMap ||
        type == EObjectType::DataMissingChunkMap ||
        type == EObjectType::ParityMissingChunkMap ||
        type == EObjectType::OldestPartMissingChunkMap ||
        type == EObjectType::QuorumMissingChunkMap ||
        type == EObjectType::UnsafelyPlacedChunkMap ||
        type == EObjectType::InconsistentlyPlacedChunkMap ||
        type == EObjectType::ForeignChunkMap ||
        type == EObjectType::RackMap ||
        type == EObjectType::DataCenterMap ||
        type == EObjectType::HostMap ||
        type == EObjectType::ChunkLocationMap ||
        type == EObjectType::ChunkListMap ||
        type == EObjectType::ChunkViewMap ||
        type == EObjectType::MediumMap ||
        type == EObjectType::TransactionMap ||
        type == EObjectType::TopmostTransactionMap ||
        type == EObjectType::ClusterNodeNode ||
        type == EObjectType::LegacyClusterNodeMap ||
        type == EObjectType::ClusterNodeMap ||
        type == EObjectType::DataNodeMap ||
        type == EObjectType::ExecNodeMap ||
        type == EObjectType::TabletNodeMap ||
        type == EObjectType::ChaosNodeMap ||
        type == EObjectType::Orchid ||
        type == EObjectType::LostVitalChunkMap ||
        type == EObjectType::PrecariousVitalChunkMap ||
        type == EObjectType::AccountMap ||
        type == EObjectType::UserMap ||
        type == EObjectType::GroupMap ||
        type == EObjectType::AccountResourceUsageLeaseMap ||
        type == EObjectType::SchedulerPoolTreeMap ||
        type == EObjectType::Link ||
        type == EObjectType::AccessControlNode ||
        type == EObjectType::Document ||
        type == EObjectType::LockMap ||
        type == EObjectType::TabletMap ||
        type == EObjectType::TabletCellMap ||
        type == EObjectType::TabletCellNode ||
        type == EObjectType::TabletCellBundleMap ||
        type == EObjectType::TabletActionMap ||
        type == EObjectType::AreaMap ||
        type == EObjectType::ChaosCellMap ||
        type == EObjectType::ChaosCellBundleMap ||
        type == EObjectType::SysNode ||
        type == EObjectType::PortalEntrance ||
        type == EObjectType::PortalExit ||
        type == EObjectType::PortalEntranceMap ||
        type == EObjectType::PortalExitMap ||
        type == EObjectType::CypressShardMap ||
        type == EObjectType::EstimatedCreationTimeMap ||
        type == EObjectType::NetworkProjectMap ||
        type == EObjectType::HttpProxyRoleMap ||
        type == EObjectType::RpcProxyRoleMap ||
        type == EObjectType::MasterTableSchemaMap ||
        type == EObjectType::ChaosReplicatedTable;
}

bool IsUserType(EObjectType type)
{
    return
        type == EObjectType::Transaction ||
        type == EObjectType::Chunk ||
        type == EObjectType::JournalChunk ||
        type == EObjectType::ErasureChunk ||
        type == EObjectType::ErasureJournalChunk ||
        type == EObjectType::ChunkList ||
        type == EObjectType::StringNode ||
        type == EObjectType::Int64Node ||
        type == EObjectType::Uint64Node ||
        type == EObjectType::DoubleNode ||
        type == EObjectType::BooleanNode ||
        type == EObjectType::MapNode ||
        type == EObjectType::ListNode ||
        type == EObjectType::File ||
        type == EObjectType::Table ||
        type == EObjectType::ReplicatedTable ||
        type == EObjectType::ReplicationLogTable ||
        type == EObjectType::TableReplica ||
        type == EObjectType::TabletAction ||
        type == EObjectType::Journal ||
        type == EObjectType::Link ||
        type == EObjectType::AccessControlNode ||
        type == EObjectType::Document ||
        type == EObjectType::Account ||
        type == EObjectType::SchedulerPool ||
        type == EObjectType::SchedulerPoolTree ||
        type == EObjectType::ChaosReplicatedTable;
}

bool IsTableType(EObjectType type)
{
    return
        type == EObjectType::Table ||
        type == EObjectType::ReplicatedTable ||
        type == EObjectType::ReplicationLogTable;
}

bool IsLogTableType(EObjectType type)
{
    return
        type == EObjectType::ReplicatedTable ||
        type == EObjectType::ReplicationLogTable;
}

bool IsCellType(EObjectType type)
{
    return
        type == EObjectType::TabletCell ||
        type == EObjectType::ChaosCell;
}

bool IsCellBundleType(EObjectType type)
{
    return
        type == EObjectType::TabletCellBundle ||
        type == EObjectType::ChaosCellBundle;
}

bool IsAlienType(EObjectType type)
{
    return type == EObjectType::ChaosCell;
}

bool HasSchema(EObjectType type)
{
    if (type == EObjectType::Master) {
        return false;
    }
    if (IsSchemaType(type)) {
        return false;
    }
    return true;
}

bool IsSchemaType(EObjectType type)
{
    return (static_cast<ui32>(type) & SchemaObjectTypeMask) != 0;
}

bool IsGlobalCellId(TCellId cellId)
{
    auto type = TypeFromId(cellId);
    return
        type == EObjectType::MasterCell ||
        type == EObjectType::ChaosCell;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NObjectClient

