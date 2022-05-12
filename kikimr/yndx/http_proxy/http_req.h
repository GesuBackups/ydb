#pragma once

#include <ydb/core/protos/serverless_proxy_config.pb.h>
#include <ydb/library/http_proxy/authorization/signature.h>

#include <ydb/core/http_proxy/events.h>
#include <ydb/core/http_proxy/http_req.h>

namespace NKikimr::NHttpProxy {

NActors::IActor* CreateAccessServiceActor(const NKikimrConfig::TServerlessProxyConfig& config);
NActors::IActor* CreateIamTokenServiceActor(const NKikimrConfig::TServerlessProxyConfig& config);
NActors::IActor* CreateIamAuthActor(const TActorId sender, THttpRequestContext& context, THolder<NKikimr::NSQS::TAwsRequestSignV4>&& signature);


} // namespace NKinesis::NHttpProxy
