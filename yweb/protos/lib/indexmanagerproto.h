#pragma once

#include "metric.h"

#include <library/cpp/messagebus/ybus.h>
#include <library/cpp/messagebus/protobuf/ybusbuf.h>

class TIndexManagerProtocol: public NBus::TBusBufferProtocol {
public:
    TIndexManagerProtocol()
        : NBus::TBusBufferProtocol("INDEXMANAGER", 40445)
    {
        RegisterType(new NBus::TBusMetricRequest());
        RegisterType(new NBus::TBusMetricResponse());
    }

    /// returns key for messages of this protocol
    NBus::TBusKey GetKey(const NBus::TBusMessage *) override {
        return NBus::YBUS_KEYLOCAL;
    }
};
