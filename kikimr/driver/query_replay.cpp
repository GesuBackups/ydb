#include "query_replay.h"
#include <ydb/core/base/appdata.h>
#include <ydb/core/util/counted_leaky_bucket.h>
#include <ydb/core/kqp/counters/kqp_counters.h>
#include <logbroker/unified_agent/client/cpp/client.h>
#include <library/cpp/logger/global/global.h>

namespace NKikimr {

namespace NKqp {

class TUnifiedAgentQueryReplayBackend : public IQueryReplayBackend {
public:
    TUnifiedAgentQueryReplayBackend(const NKikimrConfig::TTableServiceConfig& serviceConfig, TIntrusivePtr<TKqpCounters> kqpCounters) {
        const auto& config = serviceConfig.GetQueryReplayConfig();
        NUnifiedAgent::TClientParameters clientParams(config.GetUnifiedAgentUri());
        UnifiedAgentLogger.Reset(CreateDefaultLogger<TNullLog>());
        clientParams.SetLog(*UnifiedAgentLogger);
        clientParams.SetGrpcMaxMessageSize(config.GetGrpcMaxMessageSizeMB() << 20);
        clientParams.SetMaxInflightBytes(config.GetInflightLimitMB() << 20);
        UnifiedAgentClient = NUnifiedAgent::MakeClient(clientParams);
        NUnifiedAgent::TSessionParameters sessionSettings;
        sessionSettings.SetCounters(kqpCounters->GetQueryReplayCounters());
        UnifiedAgentSession = UnifiedAgentClient->CreateSession(sessionSettings);
        UpdateConfig(serviceConfig);
    }

    void Collect(const TString& queryData) {
        auto now = TAppData::TimeProvider->Now();
        LeakyBucketQuota->Update(now);
        if (!LeakyBucketQuota->TryPush(now, 1.0)) {
            return;
        }

        UnifiedAgentSession->Send(NUnifiedAgent::TClientMessage{queryData, Nothing(), Nothing()});
    }

    void UpdateConfig(const NKikimrConfig::TTableServiceConfig& serviceConfig) {
        const auto& config = serviceConfig.GetQueryReplayConfig();
        double bucketSize = config.GetLeakyBucketQuotaBucketSize();
        TDuration bucketDuration = TDuration::Seconds(config.GetLeakyBucketQuotaBucketDurationSeconds());
        LeakyBucketQuota.Reset(new TCountedLeakyBucket(bucketSize, bucketDuration, TAppData::TimeProvider->Now()));
    }

    ~TUnifiedAgentQueryReplayBackend() {
        UnifiedAgentSession->Close();
    }

private:

    THolder<TLog> UnifiedAgentLogger;
    THolder<TCountedLeakyBucket> LeakyBucketQuota;
    NUnifiedAgent::TClientPtr UnifiedAgentClient;
    NUnifiedAgent::TClientSessionPtr UnifiedAgentSession;
};

} // NKqp

NKqp::IQueryReplayBackend *TQueryReplayBackendFactory::Create(
        const NKikimrConfig::TTableServiceConfig& serviceConfig,
        TIntrusivePtr<NKqp::TKqpCounters> counters)
{
    const auto& queryReplayConfig = serviceConfig.GetQueryReplayConfig();
    if (!queryReplayConfig.GetEnabled()) {
        return new NKqp::TNullQueryReplayBackend();
    } else {
        return new NKqp::TUnifiedAgentQueryReplayBackend(serviceConfig, counters);
    }
}

} // NKikimr
