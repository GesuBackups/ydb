#include "ydb_credentials_provider_factory.h"

#include <kikimr/public/sdk/cpp/client/iam/iam.h>

namespace NKikimr {

std::shared_ptr<NYdb::ICredentialsProviderFactory> CreateYndxYdbCredentialsProviderFactory(const TYdbCredentialsSettings& settings) {
    if (settings.UseLocalMetadata) {
        return NYdb::CreateIamCredentialsProviderFactory();
    } else if (settings.SaKeyFile) {
        NYdb::TIamJwtFilename params = {.JwtFilename = settings.SaKeyFile};

        if (settings.IamEndpoint)
            params.Endpoint = settings.IamEndpoint;

        return NYdb::CreateIamJwtFileCredentialsProviderFactory(std::move(params));
    }

    return NYdb::CreateOAuthCredentialsProviderFactory({settings.OAuthToken});
}

}
