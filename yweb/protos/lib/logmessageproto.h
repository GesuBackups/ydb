#pragma once

#include <library/cpp/messagebus/protobuf/ybusbuf.h>

#include <yweb/protos/spider.pb.h>


// logel message
class TBusLogelMessage : public NBus::TBusBufferMessage<NFetcherMsg::TLogelMessage, 103> {
public:
    TBusLogelMessage()
        : Cookie(nullptr)
    {
    }
    void *Cookie;
};

// logel message bus protocol
class TBusLogelProtocol : public NBus::TBusBufferProtocol {
public:
    static const int DefaultPort = 10201;

public:
    TBusLogelProtocol(int port = 0, const char *service = "")
        : NBus::TBusBufferProtocol(service, (port > 0) ? port : DefaultPort)
    {
        RegisterType(new TBusLogelMessage());
    }
};
