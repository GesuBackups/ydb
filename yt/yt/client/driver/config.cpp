#include "config.h"

#include <yt/yt/client/api/config.h>

#include <yt/yt/client/chunk_client/config.h>

#include <yt/yt/client/table_client/config.h>

#include <yt/yt/core/misc/cache_config.h>

namespace NYT::NDriver {

////////////////////////////////////////////////////////////////////////////////

TDriverConfig::TDriverConfig()
{
    RegisterParameter("file_reader", FileReader)
        .DefaultNew();
    RegisterParameter("file_writer", FileWriter)
        .DefaultNew();
    RegisterParameter("table_reader", TableReader)
        .DefaultNew();
    RegisterParameter("table_writer", TableWriter)
        .DefaultNew();
    RegisterParameter("journal_reader", JournalReader)
        .DefaultNew();
    RegisterParameter("journal_writer", JournalWriter)
        .DefaultNew();
    RegisterParameter("fetcher", Fetcher)
        .DefaultNew();

    RegisterParameter("read_buffer_row_count", ReadBufferRowCount)
        .Default((i64) 10000);
    RegisterParameter("read_buffer_size", ReadBufferSize)
        .Default((i64) 1 * 1024 * 1024);
    RegisterParameter("write_buffer_size", WriteBufferSize)
        .Default((i64) 1 * 1024 * 1024);

    RegisterParameter("client_cache", ClientCache)
        .DefaultNew();

    RegisterParameter("api_version", ApiVersion)
        .Default(ApiVersion3)
        .GreaterThanOrEqual(ApiVersion3)
        .LessThanOrEqual(ApiVersion4);

    RegisterParameter("token", Token)
        .Optional();

    RegisterParameter("proxy_discovery_cache", ProxyDiscoveryCache)
        .DefaultNew();

    RegisterPreprocessor([&] {
        ClientCache->Capacity = 1024_KB;
        ProxyDiscoveryCache->RefreshTime = TDuration::Seconds(15);
        ProxyDiscoveryCache->ExpireAfterSuccessfulUpdateTime = TDuration::Seconds(15);
        ProxyDiscoveryCache->ExpireAfterFailedUpdateTime = TDuration::Seconds(15);
    });

    RegisterPostprocessor([&] {
        if (ApiVersion != ApiVersion3 && ApiVersion != ApiVersion4) {
            THROW_ERROR_EXCEPTION("Unsupported API version %v",
                ApiVersion);
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDriver

