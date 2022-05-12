#include "config.h"

namespace NYT::NApi::NRpcProxy {

////////////////////////////////////////////////////////////////////////////////

TConnectionConfig::TConnectionConfig()
{
    RegisterParameter("cluster_url", ClusterUrl)
        .Default();
    RegisterParameter("proxy_role", ProxyRole)
        .Optional();
    RegisterParameter("proxy_addresses", ProxyAddresses)
        .Alias("addresses")
        .Optional();
    RegisterParameter("proxy_endpoints", ProxyEndpoints)
        .Optional();
    RegisterParameter("proxy_host_order", ProxyHostOrder)
        .Optional();

    RegisterParameter("dynamic_channel_pool", DynamicChannelPool)
        .DefaultNew();

    RegisterParameter("ping_period", PingPeriod)
        .Default(TDuration::Seconds(3));

    RegisterParameter("proxy_list_update_period", ProxyListUpdatePeriod)
        .Default(TDuration::Minutes(5));
    RegisterParameter("proxy_list_retry_period", ProxyListRetryPeriod)
        .Default(TDuration::Seconds(1));
    RegisterParameter("max_proxy_list_retry_period", MaxProxyListRetryPeriod)
        .Default(TDuration::Seconds(30));
    RegisterParameter("max_proxy_list_update_attempts", MaxProxyListUpdateAttempts)
        .Default(3);

    RegisterParameter("rpc_timeout", RpcTimeout)
        .Default(TDuration::Seconds(30));
    RegisterParameter("rpc_acknowledgement_timeout", RpcAcknowledgementTimeout)
        .Default(TDuration::Seconds(15));
    RegisterParameter("timestamp_provider_latest_timestamp_update_period", TimestampProviderLatestTimestampUpdatePeriod)
        .Default(TDuration::Seconds(3));
    RegisterParameter("default_transaction_timeout", DefaultTransactionTimeout)
        .Default(TDuration::Seconds(30));
    RegisterParameter("default_lookup_rows_timeout", DefaultLookupRowsTimeout)
        .Default(TDuration::Seconds(30));
    RegisterParameter("default_select_rows_timeout", DefaultSelectRowsTimeout)
        .Default(TDuration::Seconds(30));
    RegisterParameter("default_total_streaming_timeout", DefaultTotalStreamingTimeout)
        .Default(TDuration::Minutes(15));
    RegisterParameter("default_streaming_stall_timeout", DefaultStreamingStallTimeout)
        .Default(TDuration::Minutes(1));

    RegisterParameter("default_ping_period", DefaultPingPeriod)
        .Default(TDuration::Seconds(5));

    RegisterParameter("bus_client", BusClient)
        .DefaultNew();
    RegisterParameter("idle_channel_ttl", IdleChannelTtl)
        .Default(TDuration::Minutes(5));

    RegisterParameter("http_client", HttpClient)
        .DefaultNew();

    RegisterParameter("request_codec", RequestCodec)
        .Default(NCompression::ECodec::None);
    RegisterParameter("response_codec", ResponseCodec)
        .Default(NCompression::ECodec::None);
    // COMPAT(kiselyovp): legacy RPC codecs
    RegisterParameter("enable_legacy_rpc_codecs", EnableLegacyRpcCodecs)
        .Default(true);

    RegisterParameter("enable_retries", EnableRetries)
        .Default(false);
    RegisterParameter("retrying_channel", RetryingChannel)
        .DefaultNew();

    RegisterParameter("modify_rows_batch_capacity", ModifyRowsBatchCapacity)
        .GreaterThanOrEqual(0)
        .Default(0);

    RegisterPreprocessor([&] {
        DynamicChannelPool->MaxPeerCount = 100;
    });

    RegisterPostprocessor([&] {
        if (!ProxyEndpoints && !ClusterUrl && !ProxyAddresses) {
            THROW_ERROR_EXCEPTION("Either \"endpoints\" or \"cluster_url\" or \"proxy_addresses\" must be specified");
        }
        if (ProxyEndpoints && ProxyRole) {
            THROW_ERROR_EXCEPTION("\"proxy_role\" is not supported by Service Discovery");
        }
        if (ProxyAddresses && ProxyAddresses->empty()) {
            THROW_ERROR_EXCEPTION("\"proxy_addresses\" must not be empty");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy
