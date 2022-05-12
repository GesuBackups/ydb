#pragma once

#include <yt/yt/client/api/client.h>

#include <yt/yt/core/profiling/public.h>


// @brief    IPenaltyProvider interface is used in HedgingClient to provide external penalties for different clusters.
//           Current implementations are DummyPenaltyProvider and ReplicaionLagPenaltyProvider.
namespace NYT::NClient::NHedging::NRpc {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(IPenaltyProvider);

class IPenaltyProvider
    : public TRefCounted 
{
public:
    virtual NProfiling::TCpuDuration Get(const TString& cluster) = 0;
};

DEFINE_REFCOUNTED_TYPE(IPenaltyProvider);

////////////////////////////////////////////////////////////////////////////////

// @brief DummyPenaltyProvider - always returns 0.
IPenaltyProviderPtr CreateDummyPenaltyProvider();

// From config.proto.
class TReplicaionLagPenaltyProviderConfig;

// @brief ReplicaionLagPenaltyProvider - perodically checks replication lag for given table AND replica cluster.
//        Based on values from TReplicaionLagPenaltyProviderConfig add current number of tablets with lag, it either returns 0 or LagPenalty value.
//        Master client - main cluster with replicated table. ReplicaCluster + TablePath specifies concrete replica for table from main cluster.
IPenaltyProviderPtr CreateReplicaionLagPenaltyProvider(
    const TReplicaionLagPenaltyProviderConfig& config, NApi::IClientPtr masterClient);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClient::NHedging::NRpc
