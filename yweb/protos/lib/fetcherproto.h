#pragma once

#include <yweb/protos/spider.pb.h>
#include <library/cpp/messagebus/protobuf/ybusbuf.h>

enum {
    // fetcherproto
    MTYPE_FETCHER_REQUEST  = 100,
    MTYPE_FETCHER_RESPONSE = 101,
};

const unsigned MAX_BUS_MESSAGE_SIZE_FETCHER = 55 << 20;

typedef NBus::TBusBufferMessage<NFetcherMsg::TFetchRequest, MTYPE_FETCHER_REQUEST> TBusFetcherRequest;
typedef NBus::TBusBufferMessage<NFetcherMsg::TFetchResponse, MTYPE_FETCHER_RESPONSE> TBusFetcherResponse;

typedef NBus::TBusBufferMessagePtr<TBusFetcherRequest> TBusFetcherRequestPtr;
typedef NBus::TBusBufferMessagePtr<TBusFetcherResponse> TBusFetcherResponsePtr;

class TFetcherProtocol: public NBus::TBusBufferProtocol {
public:
    TFetcherProtocol(NBus::TBusService name = "FETCHER", int port = TFetcherProtocol::DefaultPort)
        : NBus::TBusBufferProtocol(name, port)
    {
        RegisterType( new TBusFetcherRequest() );
        RegisterType( new TBusFetcherResponse() );
    }

    static const int DefaultPort = 33444;
};

inline const char *ConstraintId2String(NFetcherMsg::EConstraintId id, bool compat = false) {
    using namespace NFetcherMsg;
    if (!compat) {
        switch (id) {
        case CID_NO_CONSTRAINT:
            return "";
        case CID_QUOTA_NO_SOURCENAME:
            return "source name isn't in the config";
        case CID_QUOTA_NO_REQTYPE:
            return "request type isn't in the config";
        case CID_QUOTA_NO_IP:
            return "ip isn't in the config";
        case CID_QUOTA_REQTYPE_EXCEEDED:
            return "request type quota is exceeded";
        case CID_QUOTA_IP_EXCEEDED:
            return "ip quota is exceeded";
        case CID_TIMEOUT:
            return "request timed out";
        case CID_NO_SPACE:
            return "no space for a new host";
        case CID_HOST_RENEWED_RECENTLY:
            return "host has beed renewed recently";
        case CID_NO_HOST:
            return "no such host";
        case CID_HOST_BUSY:
            return "hostname is too busy";
        case CID_IP_BUSY:
            return "ip is too busy";
        case CID_MAXINFLIGHT_EXCEEDED:
            return "maxinflight is exceeded";
        case CID_PROCESSING_QUEUE_IS_FULL:
            return "queue for processing is full";
        case CID_DNS_QUEUE_IS_FULL:
            return "queue for dns resolving is full";
        case CID_INCONSISTENT_CLUSTER:
            return "inconsistent zora cluster vision";
        case CID_SPIDER_SEND_ERROR:
            return "mb error on sending message to spider";
        case CID_ZORACL_ERROR:
            return "zoracl error";
        case CID_KWCALC_ERROR:
            return "kwcalc error";
        case CID_AUTH_FAILED:
            return "auth failed";
        default:
            return "unknown constraint";
        }
    } else { // compatibility mode
        switch (id) {
        case CID_NO_CONSTRAINT:
            return "";
        case CID_QUOTA_NO_SOURCENAME:
            return "no source in the config";
        case CID_QUOTA_NO_REQTYPE:
            return "no source in the config"; // there were no request type in older configs
        case CID_QUOTA_NO_IP:
            return "no rule for ip in the config";
        case CID_QUOTA_REQTYPE_EXCEEDED:
            return "source quota exceeded";
        case CID_QUOTA_IP_EXCEEDED:
            return "ip quota exceeded";
        case CID_TIMEOUT:
            return "timeout";
        case CID_NO_SPACE:
            return "no space";
        case CID_HOST_RENEWED_RECENTLY:
            return "host renewed recently";
        case CID_NO_HOST:
            return "no host: ";
        case CID_HOST_BUSY:
            return "hostname: ";
        case CID_IP_BUSY:
            return "ip: ";
        case CID_MAXINFLIGHT_EXCEEDED:
            return "maxinflight quota exceeded";
        case CID_PROCESSING_QUEUE_IS_FULL:
            return "server processing queue quota excedeed";
        case CID_DNS_QUEUE_IS_FULL:
            return "dns retrieving queue quota excedeed";
        case CID_INCONSISTENT_CLUSTER:
            return "Internal error: inconsitent zora cluster vision.";
        case CID_SPIDER_SEND_ERROR:
            return "error on sending message to spider";
        case CID_ZORACL_ERROR:
            return "zoracl error";
        case CID_KWCALC_ERROR:
            return "kwcalc error";
        default:
            return "unknown constraint";
        }
    }
}

inline const char *RequestType2Str(NFetcherMsg::ERequestType t) {
    switch (t) {
    case NFetcherMsg::ONLINE:
        return "online";
    case NFetcherMsg::OFFLINE:
        return "offline";
    case NFetcherMsg::USERPROXY:
        return "userproxy";
    default:
        return nullptr;
    }
}

