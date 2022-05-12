#pragma once

#include <ydb/core/persqueue/actor_persqueue_client_iface.h>
#include <kikimr/public/sdk/cpp/client/iam/iam.h>

namespace NKikimr::NPQ {

class TYndxPersQueueMirrorReaderFactory : public TPersQueueMirrorReaderFactory {
public:
    std::shared_ptr<NYdb::ICredentialsProviderFactory> GetCredentialsProvider(
        const NKikimrPQ::TMirrorPartitionConfig::TCredentials& cred
    ) const override {
        switch (cred.GetCredentialsCase()) {
            case NKikimrPQ::TMirrorPartitionConfig::TCredentials::kOauthToken: {
                return NYdb::CreateOAuthCredentialsProviderFactory(cred.GetOauthToken());
            }
            case NKikimrPQ::TMirrorPartitionConfig::TCredentials::kIam: {
                NYdb::TIamJwtContent params;
                params.JwtContent = cred.GetIam().GetServiceAccountKey();
                params.Endpoint = cred.GetIam().GetEndpoint();
                return NYdb::CreateIamJwtParamsCredentialsProviderFactory(params);
            }
            case NKikimrPQ::TMirrorPartitionConfig::TCredentials::kJwtParams: {
                NYdb::TIamJwtContent params;
                params.JwtContent = cred.GetJwtParams();
                return NYdb::CreateIamJwtParamsCredentialsProviderFactory(params);
            }
            default: {
                return TPersQueueMirrorReaderFactory::GetCredentialsProvider(cred);
            }
        }
    }
};

} // namespace NKikimr::NSQS
