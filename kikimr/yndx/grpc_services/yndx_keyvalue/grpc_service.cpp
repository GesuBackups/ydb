#include "grpc_service.h"

#include <ydb/core/grpc_services/grpc_helper.h>
#include <ydb/core/grpc_services/base/base.h>
#include <kikimr/yndx/grpc_services/rpc_calls/service_yndx_keyvalue.h>


namespace NKikimr::NGRpcService {

TYndxKeyValueGRpcService::TYndxKeyValueGRpcService(NActors::TActorSystem* actorSystem, TIntrusivePtr<NMonitoring::TDynamicCounters> counters, NActors::TActorId grpcRequestProxyId)
    : ActorSystem(actorSystem)
    , Counters(std::move(counters))
    , GRpcRequestProxyId(grpcRequestProxyId)
{
}

TYndxKeyValueGRpcService::~TYndxKeyValueGRpcService() = default;

void TYndxKeyValueGRpcService::InitService(grpc::ServerCompletionQueue* cq, NGrpc::TLoggerPtr logger) {
    CQ = cq;
    SetupIncomingRequests(std::move(logger));
}

void TYndxKeyValueGRpcService::SetGlobalLimiterHandle(NGrpc::TGlobalLimiter* limiter) {
    Limiter = limiter;
}

bool TYndxKeyValueGRpcService::IncRequest() {
    return Limiter->Inc();
}

void TYndxKeyValueGRpcService::DecRequest() {
    Limiter->Dec();
}

void TYndxKeyValueGRpcService::SetupIncomingRequests(NGrpc::TLoggerPtr logger) {
    auto getCounterBlock = NGRpcService::CreateCounterCb(Counters, ActorSystem);

#ifdef SETUP_METHOD
#error SETUP_METHOD macro collision
#endif

#define SETUP_METHOD(methodName, method, rlMode)                                                             \
    MakeIntrusive<NGRpcService::TGRpcRequest<                                                                \
        Ydb::Yndx::KeyValue::Y_CAT(methodName, Request),                                                  \
        Ydb::Yndx::KeyValue::Y_CAT(methodName, Response),                                                 \
        TYndxKeyValueGRpcService>>                                                                        \
    (                                                                                                        \
        this,                                                                                                \
        &Service_,                                                                                           \
        CQ,                                                                                                  \
        [this](NGrpc::IRequestContextBase* reqCtx) {                                                         \
            NGRpcService::ReportGrpcReqToMon(*ActorSystem, reqCtx->GetPeer());                               \
            ActorSystem->Send(GRpcRequestProxyId, new TGrpcRequestOperationCall<                             \
                Ydb::Yndx::KeyValue::Y_CAT(methodName, Request),                                          \
                Ydb::Yndx::KeyValue::Y_CAT(methodName, Response)>(reqCtx, &method,                        \
                    TRequestAuxSettings{rlMode, nullptr}));                                                  \
        },                                                                                                   \
        &Ydb::Yndx::KeyValue::V1::KeyValueService::AsyncService::Y_CAT(Request, methodName),       \
        "YndxKeyValue/" Y_STRINGIZE(methodName),                                                          \
        logger,                                                                                              \
        getCounterBlock("keyvalue", Y_STRINGIZE(methodName))                                             \
    )->Run()

    SETUP_METHOD(CreateVolume, DoCreateVolumeYndxKeyValue, TRateLimiterMode::Rps);
    SETUP_METHOD(DropVolume, DoDropVolumeYndxKeyValue, TRateLimiterMode::Rps);
    SETUP_METHOD(ListLocalPartitions, DoListLocalPartitionsYndxKeyValue, TRateLimiterMode::Rps);

    SETUP_METHOD(AcquireLock, DoAcquireLockYndxKeyValue, TRateLimiterMode::Rps);
    SETUP_METHOD(ExecuteTransaction, DoExecuteTransactionYndxKeyValue, TRateLimiterMode::Rps);
    SETUP_METHOD(Read, DoReadYndxKeyValue, TRateLimiterMode::Rps);
    SETUP_METHOD(ReadRange, DoReadRangeYndxKeyValue, TRateLimiterMode::Rps);
    SETUP_METHOD(ListRange, DoListRangeYndxKeyValue, TRateLimiterMode::Rps);
    SETUP_METHOD(GetStorageChannelStatus, DoGetStorageChannelStatusYndxKeyValue, TRateLimiterMode::Rps);

#undef SETUP_METHOD
}

} // namespace NKikimr::NGRpcService
