#pragma once

#include <logbroker/unified_agent/common/util/dynamic_counters_wrapper.h>

namespace NUnifiedAgent {
    struct TClientSessionCounters;

    struct TClientCounters: public TDynamicCountersWrapper {
        explicit TClientCounters(const NMonitoring::TDynamicCounterPtr& counters =
                                 MakeIntrusive<NMonitoring::TDynamicCounters>());

        NMonitoring::TDeprecatedCounter& ActiveSessionsCount;
        NMonitoring::TDeprecatedCounter& ClientLogDroppedBytes;

    public:
        TIntrusivePtr<TClientSessionCounters> GetDefaultSessionCounters();
    };

    struct TClientSessionCounters: public TDynamicCountersWrapper {
        explicit TClientSessionCounters(const NMonitoring::TDynamicCounterPtr& counters =
                                        MakeIntrusive<NMonitoring::TDynamicCounters>());

        NMonitoring::TDeprecatedCounter& ReceivedMessages;
        NMonitoring::TDeprecatedCounter& ReceivedBytes;
        NMonitoring::TDeprecatedCounter& AcknowledgedMessages;
        NMonitoring::TDeprecatedCounter& AcknowledgedBytes;
        NMonitoring::TDeprecatedCounter& InflightMessages;
        NMonitoring::TDeprecatedCounter& InflightBytes;
        NMonitoring::TDeprecatedCounter& GrpcWriteBatchRequests;
        NMonitoring::TDeprecatedCounter& GrpcInflightMessages;
        NMonitoring::TDeprecatedCounter& GrpcInflightBytes;
        NMonitoring::TDeprecatedCounter& GrpcCalls;
        NMonitoring::TDeprecatedCounter& GrpcCallsInitialized;
        NMonitoring::TDeprecatedCounter& DroppedMessages;
        NMonitoring::TDeprecatedCounter& DroppedBytes;
        NMonitoring::TDeprecatedCounter& ErrorsCount;
    };
}
