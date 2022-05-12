#pragma once

#include "metric.h"

#include <yweb/protos/indexeddoc.pb.h>

#include <library/cpp/digest/old_crc/crc.h>
#include <library/cpp/messagebus/protobuf/ybusbuf.h>

namespace NRealTime {

enum {
    // indexedproto
    MTYPE_INDEXEDDOC_REQUEST  = 33470,
    MTYPE_INDEXEDDOC_RESPONSE = 33471,
    MTYPE_INDEXEDDOC_DEPOSIT  = 33472,
    MTYPE_INDEXEDDOC_DEPSLIP  = 33473,
    MTYPE_INDEXEDDOC_BULKRESPONSE = 33474,
    MTYPE_FETCH_REQUEST = 33475,
    MTYPE_CLIENTINFO_REQUEST = 33476,
    MTYPE_CLIENTINFO_RESPONSE = 33477,
};

typedef NBus::TBusBufferMessage<TIndexedDocRequest,  MTYPE_INDEXEDDOC_REQUEST>  TBusIndexedDocRequest;
typedef NBus::TBusBufferMessage<TIndexedDocResponse, MTYPE_INDEXEDDOC_RESPONSE> TBusIndexedDocResponse;
typedef NBus::TBusBufferMessage<TIndexedDocDeposit,  MTYPE_INDEXEDDOC_DEPOSIT>  TBusIndexedDocDeposit;
typedef NBus::TBusBufferMessage<TIndexedDocDepSlip,  MTYPE_INDEXEDDOC_DEPSLIP>  TBusIndexedDocDepSlip;
typedef NBus::TBusBufferMessage<TIndexedDocBulkResponse, MTYPE_INDEXEDDOC_BULKRESPONSE> TBusIndexedDocBulkResponse;
typedef NBus::TBusBufferMessage<TFetchRequest, MTYPE_FETCH_REQUEST> TBusFetchRequest;
typedef NBus::TBusBufferMessage<TClientInfoRequest, MTYPE_CLIENTINFO_REQUEST> TBusClientInfoRequest;
typedef NBus::TBusBufferMessage<TClientInfoResponse, MTYPE_CLIENTINFO_RESPONSE> TBusClientInfoResponse;

typedef NBus::TBusBufferMessagePtr<TBusIndexedDocRequest>  TBusIndexedDocRequestPtr;
typedef NBus::TBusBufferMessagePtr<TBusIndexedDocResponse> TBusIndexedDocResponsePtr;
typedef NBus::TBusBufferMessagePtr<TBusIndexedDocDeposit>  TBusIndexedDocDepositPtr;
typedef NBus::TBusBufferMessagePtr<TBusIndexedDocDepSlip>  TBusIndexedDocDepSlipPtr;
typedef NBus::TBusBufferMessagePtr<TBusIndexedDocBulkResponse> TBusIndexedDocBulkResponsePtr;
typedef NBus::TBusBufferMessagePtr<TBusFetchRequest> TBusFetchRequestPtr;
typedef NBus::TBusBufferMessagePtr<TBusClientInfoRequest> TBusClientInfoRequestPtr;
typedef NBus::TBusBufferMessagePtr<TBusClientInfoResponse> TBusClientInfoResponsePtr;

class TIndexedDocProtocol: public NBus::TBusBufferProtocol {
public:
    TIndexedDocProtocol(const char *service = "")
        : NBus::TBusBufferProtocol(service && *service ? service : "INDEXEDDOC", TIndexedDocProtocol::DefaultPort)
    {
        RegisterType( new TBusIndexedDocRequest() );
        RegisterType( new TBusIndexedDocResponse() );
        RegisterType( new TBusIndexedDocDeposit() );
        RegisterType( new TBusIndexedDocDepSlip() );
        RegisterType( new TBusIndexedDocBulkResponse() );
        RegisterType( new TBusFetchRequest() );
        RegisterType( new TBusClientInfoRequest() );
        RegisterType( new TBusClientInfoResponse() );
        RegisterType( new NBus::TBusMetricRequest() );
        RegisterType( new NBus::TBusMetricResponse() );
    }

    /// returns key for messages of this protocol
    NBus::TBusKey GetKey(const NBus::TBusMessage *mess) override {
        NBus::TBusMetricRequestPtr mreq = NBus::TBusMetricRequestPtr::DynamicCast((NBus::TBusMessage*) mess);
        if (mreq != nullptr) {
            return NBus::YBUS_KEYLOCAL;
        }
        TBusIndexedDocRequestPtr req = TBusIndexedDocRequestPtr::DynamicCast((NBus::TBusMessage*) mess);
        if (req != nullptr) {
            if (req->HasRangeStart()) {
                return NBus::TBusKey(req->GetRangeStart());
            }
            return NBus::TBusKey(0);
        }
        TBusFetchRequestPtr fetch = TBusFetchRequestPtr::DynamicCast((NBus::TBusMessage*) mess);
        if (fetch != nullptr) {
            return NBus::TBusKey(fetch->GetKey());
        }
        TBusIndexedDocDepositPtr dep = TBusIndexedDocDepositPtr::DynamicCast((NBus::TBusMessage*) mess);
        if (dep != nullptr) {
            if( dep->HasUrlId() ) {
                TString url = dep->GetUrlId();
                NBus::TBusKey urlKey = crc64(url.data(),url.size());
                return urlKey;
            }
            return NBus::TBusKey(0);
        }
        TBusClientInfoRequestPtr info = TBusClientInfoRequestPtr::DynamicCast((NBus::TBusMessage*) mess);
        if (info != nullptr) {
            return NBus::TBusKey(0);
        }

        Y_FAIL("Unknown type of message in the protocol");
        return NBus::YBUS_KEYINVALID;
    }

    static const int DefaultPort = 33470;
};

}
