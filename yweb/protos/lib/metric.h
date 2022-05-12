#pragma once

#include <yweb/protos/metric.pb.h>

#include <library/cpp/messagebus/ybus.h>
#include <library/cpp/messagebus/protobuf/ybusbuf.h>

namespace NBus {

enum {
    // metric request and response
    MTYPE_METRIC_REQUEST   = 2300,
    MTYPE_METRIC_RESPONSE  = 2301,
};

typedef NBus::TBusBufferMessage<NOrangeData::TMetricRequest, MTYPE_METRIC_REQUEST> TBusMetricRequest;
typedef NBus::TBusBufferMessage<NOrangeData::TMetricResponse, MTYPE_METRIC_RESPONSE> TBusMetricResponse;

typedef NBus::TBusBufferMessagePtr<TBusMetricRequest> TBusMetricRequestPtr;
typedef NBus::TBusBufferMessagePtr<TBusMetricResponse> TBusMetricResponsePtr;

}
