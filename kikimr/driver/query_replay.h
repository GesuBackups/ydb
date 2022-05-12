#pragma once

#include <ydb/core/kqp/kqp_query_replay.h>

namespace NKikimr {

class TQueryReplayBackendFactory : public NKqp::IQueryReplayBackendFactory {
public:
    virtual NKqp::IQueryReplayBackend *Create(
            const NKikimrConfig::TTableServiceConfig& serviceConfig,
            TIntrusivePtr<NKqp::TKqpCounters> counters);
};

} // NKikimr
