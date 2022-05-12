#include "config.h"

#include <yt/yt/core/net/config.h>

namespace NYT::NHttp {

////////////////////////////////////////////////////////////////////////////////

THttpIOConfig::THttpIOConfig()
{
    RegisterParameter("read_buffer_size", ReadBufferSize)
        .Default(128_KB);

    RegisterParameter("connection_idle_timeout", ConnectionIdleTimeout)
        .Default(TDuration::Minutes(5));

    RegisterParameter("header_read_timeout", HeaderReadTimeout)
        .Default(TDuration::Seconds(30));

    RegisterParameter("body_read_idle_timeout", BodyReadIdleTimeout)
        .Default(TDuration::Minutes(5));

    RegisterParameter("write_idle_timeout", WriteIdleTimeout)
        .Default(TDuration::Minutes(5));
}

////////////////////////////////////////////////////////////////////////////////

TServerConfig::TServerConfig()
{
    RegisterParameter("port", Port)
        .Default(80);

    RegisterParameter("max_simultaneous_connections", MaxSimultaneousConnections)
        .Default(50000);

    RegisterParameter("max_backlog_size", MaxBacklogSize)
        .Default(8192);

    RegisterParameter("bind_retry_count", BindRetryCount)
        .Default(5);

    RegisterParameter("bind_retry_backoff", BindRetryBackoff)
        .Default(TDuration::Seconds(1));

    RegisterParameter("enable_keep_alive", EnableKeepAlive)
        .Default(true);

    RegisterParameter("cancel_fiber_on_connection_close", CancelFiberOnConnectionClose)
        .Default(false);

    RegisterParameter("nodelay", NoDelay)
        .Default(true);
}

////////////////////////////////////////////////////////////////////////////////

TClientConfig::TClientConfig()
{
    RegisterParameter("dialer", Dialer)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHttp
