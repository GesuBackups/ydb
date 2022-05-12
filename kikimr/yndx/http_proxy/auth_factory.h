#pragma once


#include <ydb/core/http_proxy/auth_factory.h>
#include <ydb/library/http_proxy/authorization/signature.h>
#include <ydb/core/base/appdata.h>

namespace NKikimr::NHttpProxy {

class TIamAuthFactory : public IAuthFactory {
    using THttpConfig = NKikimrConfig::THttpProxyConfig;

public:
    void Initialize(
        NActors::TActorSystemSetup::TLocalServices&,
        const TAppData& appData,
        const THttpConfig& config,
        const NKikimrConfig::TGRpcConfig& grpcConfig) final;

    NActors::IActor* CreateAuthActor(const TActorId sender, THttpRequestContext& context, THolder<NKikimr::NSQS::TAwsRequestSignV4>&& signature) const final;

    virtual void InitTenantDiscovery(NActors::TActorSystemSetup::TLocalServices&,
        const TAppData& appData,
        const THttpConfig& config, ui16 grpcPort);

    virtual bool UseSDK() const { return false; }
};

}
