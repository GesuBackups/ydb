#include "grpc_service.h"

#include <ydb/core/grpc_services/grpc_helper.h>
#include <ydb/core/grpc_services/base/base.h>
#include <kikimr/yndx/grpc_services/rpc_calls/service_yndx_rate_limiter.h>

namespace NKikimr::NQuoter {

using namespace NGRpcService;

TYndxRateLimiterGRpcService::TYndxRateLimiterGRpcService(NActors::TActorSystem* actorSystem,
    TIntrusivePtr<NMonitoring::TDynamicCounters> counters, NActors::TActorId grpcRequestProxyId)
    : ActorSystem(actorSystem)
    , Counters(std::move(counters))
    , GRpcRequestProxyId(grpcRequestProxyId)
{
}

TYndxRateLimiterGRpcService::~TYndxRateLimiterGRpcService() = default;

void TYndxRateLimiterGRpcService::InitService(grpc::ServerCompletionQueue* cq, NGrpc::TLoggerPtr logger) {
    CQ = cq;
    SetupIncomingRequests(std::move(logger));
}

void TYndxRateLimiterGRpcService::SetGlobalLimiterHandle(NGrpc::TGlobalLimiter* limiter) {
    Limiter = limiter;
}

bool TYndxRateLimiterGRpcService::IncRequest() {
    return Limiter->Inc();
}

void TYndxRateLimiterGRpcService::DecRequest() {
    Limiter->Dec();
}

void TYndxRateLimiterGRpcService::SetupIncomingRequests(NGrpc::TLoggerPtr logger) {
    auto getCounterBlock = NGRpcService::CreateCounterCb(Counters, ActorSystem);

#ifdef SETUP_METHOD
#error SETUP_METHOD macro collision
#endif

#define SETUP_METHOD(methodName, method, rlMode)                                                             \
    MakeIntrusive<NGRpcService::TGRpcRequest<                                                                \
        Ydb::Yndx::RateLimiter::Y_CAT(methodName, Request),                                                  \
        Ydb::Yndx::RateLimiter::Y_CAT(methodName, Response),                                                 \
        TYndxRateLimiterGRpcService>>                                                                        \
    (                                                                                                        \
        this,                                                                                                \
        &Service_,                                                                                           \
        CQ,                                                                                                  \
        [this](NGrpc::IRequestContextBase* reqCtx) {                                                         \
            NGRpcService::ReportGrpcReqToMon(*ActorSystem, reqCtx->GetPeer());                               \
            ActorSystem->Send(GRpcRequestProxyId, new TGrpcRequestOperationCall<                             \
                Ydb::Yndx::RateLimiter::Y_CAT(methodName, Request),                                          \
                Ydb::Yndx::RateLimiter::Y_CAT(methodName, Response)>(reqCtx, &method,                        \
                    TRequestAuxSettings{rlMode, nullptr}));                                                  \
        },                                                                                                   \
        &Ydb::Yndx::RateLimiter::V1::YndxRateLimiterService::AsyncService::Y_CAT(Request, methodName),       \
        "YndxRateLimiter/" Y_STRINGIZE(methodName),                                                          \
        logger,                                                                                              \
        getCounterBlock("rate_limiter", Y_STRINGIZE(methodName))                                             \
    )->Run()

    SETUP_METHOD(CreateResource, DoCreateYndxRateLimiterResource, TRateLimiterMode::Rps);
    SETUP_METHOD(AlterResource, DoAlterYndxRateLimiterResource, TRateLimiterMode::Rps);
    SETUP_METHOD(DropResource, DoDropYndxRateLimiterResource, TRateLimiterMode::Rps);
    SETUP_METHOD(ListResources, DoListYndxRateLimiterResources, TRateLimiterMode::Rps);
    SETUP_METHOD(DescribeResource, DoDescribeYndxRateLimiterResource, TRateLimiterMode::Rps);
    SETUP_METHOD(AcquireResource, DoAcquireYndxRateLimiterResource, TRateLimiterMode::Off);

#undef SETUP_METHOD
}

} // namespace NKikimr::NQuoter
