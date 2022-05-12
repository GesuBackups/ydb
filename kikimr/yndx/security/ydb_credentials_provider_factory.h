#pragma once

#include <ydb/library/security/ydb_credentials_provider_factory.h>

#include <memory>

namespace NKikimr {

std::shared_ptr<NYdb::ICredentialsProviderFactory> CreateYndxYdbCredentialsProviderFactory(const TYdbCredentialsSettings& settings);

}
