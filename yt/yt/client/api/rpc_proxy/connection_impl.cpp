#include "connection_impl.h"
#include "discovery_service_proxy.h"
#include "connection_impl.h"
#include "client_impl.h"
#include "config.h"
#include "helpers.h"
#include "private.h"

#include <yt/yt/core/net/local_address.h>
#include <yt/yt/core/net/address.h>

#include <yt/yt/core/concurrency/action_queue.h>
#include <yt/yt/core/concurrency/periodic_executor.h>

#include <yt/yt/core/bus/tcp/dispatcher.h>

#include <yt/yt/core/http/client.h>
#include <yt/yt/core/http/http.h>
#include <yt/yt/core/http/helpers.h>

#include <yt/yt/core/rpc/bus/channel.h>
#include <yt/yt/core/rpc/roaming_channel.h>
#include <yt/yt/core/rpc/caching_channel_factory.h>
#include <yt/yt/core/rpc/dynamic_channel_pool.h>
#include <yt/yt/core/rpc/dispatcher.h>

#include <yt/yt/core/ytree/fluent.h>

#include <yt/yt/core/service_discovery/service_discovery.h>

namespace NYT::NApi::NRpcProxy {

using namespace NBus;
using namespace NRpc;
using namespace NNet;
using namespace NHttp;
using namespace NYson;
using namespace NYTree;
using namespace NConcurrency;
using namespace NServiceDiscovery;

////////////////////////////////////////////////////////////////////////////////

namespace {

TString NormalizeHttpProxyUrl(TString url)
{
    const TStringBuf CanonicalPrefix = "http://";
    const TStringBuf CanonicalSuffix = ".yt.yandex.net";

    if (url.find('.') == TString::npos &&
        url.find(':') == TString::npos &&
        url.find("localhost") == TString::npos)
    {
        url.append(CanonicalSuffix);
    }

    if (!url.StartsWith(CanonicalPrefix)) {
        url.prepend(CanonicalPrefix);
    }

    return url;
}

TString MakeConnectionLoggingTag(const TConnectionConfigPtr& config, TGuid connectionId)
{
    TStringBuilder builder;
    TDelimitedStringBuilderWrapper delimitedBuilder(&builder);
    if (config->ClusterUrl) {
        delimitedBuilder->AppendFormat("ClusterUrl: %v", *config->ClusterUrl);
    }
    if (config->ProxyRole) {
        delimitedBuilder->AppendFormat("ProxyRole: %v", *config->ProxyRole);
    }
    delimitedBuilder->AppendFormat("ConnectionId: %v", connectionId);
    return builder.Flush();
}

TString MakeEndpointDescription(const TConnectionConfigPtr& config, TGuid connectionId)
{
    return Format("Rpc{%v}", MakeConnectionLoggingTag(config, connectionId));
}

IAttributeDictionaryPtr MakeEndpointAttributes(const TConnectionConfigPtr& config, TGuid connectionId)
{
    return ConvertToAttributes(BuildYsonStringFluently()
        .BeginMap()
            .Item("rpc_proxy").Value(true)
            .DoIf(config->ClusterUrl.has_value(), [&] (auto fluent) {
                fluent
                    .Item("cluster_url").Value(*config->ClusterUrl);
            })
            .DoIf(config->ProxyRole.has_value(), [&] (auto fluent) {
                fluent
                    .Item("proxy_role").Value(*config->ProxyRole);
            })
            .Item("connection_id").Value(connectionId)
        .EndMap());
}

TString MakeConnectionClusterId(const TConnectionConfigPtr& config)
{
    if (config->ClusterName) {
        return Format("Rpc(Name=%v)", *config->ClusterName);
    } else if (config->ClusterUrl) {
        return Format("Rpc(Url=%v)", *config->ClusterUrl);
    } else {
        return Format("Rpc(ProxyAddresses=%v)", config->ProxyAddresses);
    }
}

class TProxyChannelProvider
    : public IRoamingChannelProvider
{
public:
    TProxyChannelProvider(
        TConnectionConfigPtr config,
        TGuid connectionId,
        TDynamicChannelPoolPtr pool,
        bool sticky)
        : Pool_(std::move(pool))
        , Sticky_(sticky)
        , EndpointDescription_(MakeEndpointDescription(config, connectionId))
        , EndpointAttributes_(MakeEndpointAttributes(config, connectionId))
    { }

    const TString& GetEndpointDescription() const override
    {
        return EndpointDescription_;
    }

    const NYTree::IAttributeDictionary& GetEndpointAttributes() const override
    {
        return *EndpointAttributes_;
    }

    TNetworkId GetNetworkId() const override
    {
        return DefaultNetworkId;
    }

    TFuture<IChannelPtr> GetChannel(const IClientRequestPtr& /*request*/) override
    {
        if (Sticky_) {
            auto guard = Guard(SpinLock_);
            if (!Channel_) {
                Channel_ = Pool_->GetRandomChannel();
            }
            return Channel_;
        } else {
            return Pool_->GetRandomChannel();
        }
    }

    void Terminate(const TError& /*error*/) override
    { }

private:
    const TDynamicChannelPoolPtr Pool_;
    const bool Sticky_;
    const TGuid ConnectionId_;

    const TString EndpointDescription_;
    const IAttributeDictionaryPtr EndpointAttributes_;

    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, SpinLock_);
    TFuture<IChannelPtr> Channel_;
};

} // namespace

////////////////////////////////////////////////////////////////////////////////

TConnection::TConnection(TConnectionConfigPtr config, TConnectionOptions options)
    : Config_(std::move(config))
    , ConnectionId_(TGuid::Create())
    , LoggingTag_(MakeConnectionLoggingTag(Config_, ConnectionId_))
    , ClusterId_(MakeConnectionClusterId(Config_))
    , Logger(RpcProxyClientLogger.WithRawTag(LoggingTag_))
    , ChannelFactory_(CreateCachingChannelFactory(
        NRpc::NBus::CreateBusChannelFactory(Config_->BusClient),
        Config_->IdleChannelTtl))
    , ChannelPool_(New<TDynamicChannelPool>(
        Config_->DynamicChannelPool,
        ChannelFactory_,
        MakeEndpointDescription(Config_, ConnectionId_),
        MakeEndpointAttributes(Config_, ConnectionId_),
        TApiServiceProxy::GetDescriptor().ServiceName,
        TDiscoverRequestHook()))
{
    if (options.ConnectionInvoker) {
        ConnectionInvoker_ = options.ConnectionInvoker;
    } else {
        ActionQueue_ = New<TActionQueue>("RpcProxyConn");
        ConnectionInvoker_ = ActionQueue_->GetInvoker();
    }

    UpdateProxyListExecutor_ = New<TPeriodicExecutor>(
        GetInvoker(),
        BIND(&TConnection::OnProxyListUpdate, MakeWeak(this)),
        Config_->ProxyListUpdatePeriod);

    Config_->Postprocess();

    if (Config_->ProxyEndpoints) {
        ServiceDiscovery_ = NRpc::TDispatcher::Get()->GetServiceDiscovery();
        if (!ServiceDiscovery_) {
            ChannelPool_->SetPeerDiscoveryError(TError("No Service Discovery is configured"));
            return;
        }
    }

    if (Config_->ProxyAddresses) {
        auto address = (*Config_->ProxyAddresses)[RandomNumber(Config_->ProxyAddresses->size())];
        DiscoveryChannel_ = ChannelFactory_->CreateChannel(address);
    }

    if (Config_->ProxyAddresses) {
        ChannelPool_->SetPeers(*Config_->ProxyAddresses);
    }
}

TConnection::~TConnection()
{
    RunNoExcept([&] {
        Terminate();
    });
}

IChannelPtr TConnection::CreateChannel(bool sticky)
{
    auto provider = New<TProxyChannelProvider>(
        Config_,
        ConnectionId_,
        ChannelPool_,
        sticky);
    return CreateRoamingChannel(std::move(provider));
}

IChannelPtr TConnection::CreateChannelByAddress(const TString& address)
{
    return ChannelFactory_->CreateChannel(address);
}

TClusterTag TConnection::GetClusterTag() const
{
    YT_ABORT();
}

const TString& TConnection::GetLoggingTag() const
{
    return LoggingTag_;
}

const TString& TConnection::GetClusterId() const
{
    return ClusterId_;
}

bool TConnection::IsSameCluster(const IConnectionPtr& other) const
{
    // NB: Cluster tag is not defined for RPC proxy connection
    // so we use some best-effort logic here.
    return GetClusterId() == other->GetClusterId();
}

IInvokerPtr TConnection::GetInvoker()
{
    return ConnectionInvoker_;
}

NApi::IClientPtr TConnection::CreateClient(const TClientOptions& options)
{
    if (options.Token) {
        DiscoveryToken_.Store(*options.Token);
    }

    if (Config_->ClusterUrl || Config_->ProxyEndpoints) {
        UpdateProxyListExecutor_->Start();
    }

    return New<TClient>(this, options);
}

NHiveClient::ITransactionParticipantPtr TConnection::CreateTransactionParticipant(
    NHiveClient::TCellId /*cellId*/,
    const TTransactionParticipantOptions& /*options*/)
{
    YT_UNIMPLEMENTED();
}

void TConnection::ClearMetadataCaches()
{ }

void TConnection::Terminate()
{
    YT_LOG_DEBUG("Terminating connection");
    ChannelPool_->Terminate(TError("Connection terminated"));
    UpdateProxyListExecutor_->Stop();
}

const TConnectionConfigPtr& TConnection::GetConfig()
{
    return Config_;
}

std::vector<TString> TConnection::DiscoverProxiesViaRpc()
{
    try {
        YT_LOG_DEBUG("Updating proxy list via RPC");

        TDiscoveryServiceProxy proxy(DiscoveryChannel_);

        auto req = proxy.DiscoverProxies();
        if (Config_->ProxyRole) {
            req->set_role(*Config_->ProxyRole);
        }
        req->SetTimeout(Config_->RpcTimeout);

        auto rsp = WaitFor(req->Invoke())
            .ValueOrThrow();

        return FromProto<std::vector<TString>>(rsp->addresses());
    } catch (const std::exception& ex) {
        DiscoveryChannel_.Reset();
        THROW_ERROR_EXCEPTION("Error discovering RPC proxies via RPC")
            << ex;
    }
}

std::vector<TString> TConnection::DiscoverProxiesViaHttp()
{
    auto correlationId = TGuid::Create();

    try {
        YT_LOG_DEBUG("Updating proxy list via HTTP (CorrelationId: %v)", correlationId);

        auto poller = TTcpDispatcher::Get()->GetXferPoller();
        auto client = NHttp::CreateClient(Config_->HttpClient, std::move(poller));
        auto headers = New<THeaders>();
        if (auto token = DiscoveryToken_.Load()) {
            headers->Add("Authorization", "OAuth " + token);
        }
        headers->Add("X-YT-Correlation-Id", ToString(correlationId));
        headers->Add("X-YT-Header-Format", "<format=text>yson");
        headers->Add(
            "X-YT-Parameters", BuildYsonStringFluently(EYsonFormat::Text)
                .BeginMap()
                    .Item("output_format")
                    .BeginAttributes()
                        .Item("format").Value("text")
                    .EndAttributes()
                    .Value("yson")
                    .OptionalItem("role", Config_->ProxyRole)
                .EndMap().ToString());

        auto url = NormalizeHttpProxyUrl(*Config_->ClusterUrl) + "/api/v4/discover_proxies";
        auto rsp = WaitFor(client->Get(url, headers))
            .ValueOrThrow();

        if (rsp->GetStatusCode() != EStatusCode::OK) {
            THROW_ERROR_EXCEPTION("HTTP proxy discovery request returned an error")
                << TErrorAttribute("correlation_id", correlationId)
                << TErrorAttribute("status_code", rsp->GetStatusCode())
                << ParseYTError(rsp);
        }

        auto body = rsp->ReadAll();
        YT_LOG_DEBUG("Received proxy list via HTTP (CorrelationId: %v)", correlationId);

        auto node = ConvertTo<INodePtr>(TYsonString(ToString(body)));
        node = node->AsMap()->FindChild("proxies");
        return ConvertTo<std::vector<TString>>(node);
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Error discovering RPC proxies via HTTP")
            << TErrorAttribute("correlation_id", correlationId)
            << ex;
    }
}

std::vector<TString> TConnection::DiscoverProxiesViaServiceDiscovery()
{
    YT_LOG_DEBUG("Updating proxy list via Service Discovery");

    std::vector<TFuture<TEndpointSet>> asyncEndpointSets;
    for (const auto& cluster : Config_->ProxyEndpoints->Clusters) {
        asyncEndpointSets.push_back(ServiceDiscovery_->ResolveEndpoints(
            cluster,
            Config_->ProxyEndpoints->EndpointSetId));
    }

    auto endpointSets = WaitFor(AllSet(asyncEndpointSets))
        .ValueOrThrow();

    std::vector<TString> allAddresses;
    std::vector<TError> errors;
    for (int i = 0; i < std::ssize(endpointSets); ++i) {
        if (!endpointSets[i].IsOK()) {
            errors.push_back(endpointSets[i]);
            YT_LOG_WARNING(
                endpointSets[i],
                "Could not resolve endpoints from cluster (Cluster: %v, EndpointSetId: %v)",
                Config_->ProxyEndpoints->Clusters[i],
                Config_->ProxyEndpoints->EndpointSetId);
            continue;
        }

        auto addresses = AddressesFromEndpointSet(endpointSets[i].Value());
        allAddresses.insert(allAddresses.end(), addresses.begin(), addresses.end());
    }

    if (errors.size() == endpointSets.size()) {
        THROW_ERROR_EXCEPTION("Error discovering RPC proxies via Service Discovery") << errors;
    }

    return allAddresses;
}

void TConnection::OnProxyListUpdate()
{
    auto attributes = CreateEphemeralAttributes();
    if (Config_->ProxyEndpoints) {
        attributes->Set("endpoint_set_cluster", Config_->ProxyEndpoints->Cluster);
        attributes->Set("endpoint_set_id", Config_->ProxyEndpoints->EndpointSetId);
    } else if (Config_->ClusterUrl) {
        attributes->Set("cluster_url", Config_->ClusterUrl);
    } else {
        attributes->Set("rpc_proxy_addresses", Config_->ProxyAddresses);
    }
    attributes->Set("proxy_role", Config_->ProxyRole.value_or(DefaultProxyRole));

    auto backoff = Config_->ProxyListRetryPeriod;
    for (int attempt = 0;; ++attempt) {
        try {
            std::vector<TString> proxies;
            if (Config_->ProxyEndpoints) {
                proxies = DiscoverProxiesViaServiceDiscovery();
            } else if (Config_->ClusterUrl) {
                proxies = DiscoverProxiesViaHttp();
            } else if (Config_->ProxyAddresses) {
                proxies = DiscoverProxiesViaRpc();
            }

            if (proxies.empty()) {
                THROW_ERROR_EXCEPTION("Proxy list is empty");
            }

            ChannelPool_->SetPeers(proxies);

            break;
        } catch (const std::exception& ex) {
            if (attempt > Config_->MaxProxyListUpdateAttempts) {
                ChannelPool_->SetPeerDiscoveryError(TError(ex) << *attributes);
            }

            YT_LOG_WARNING(ex, "Error updating proxy list (Attempt: %v, Backoff: %v)",
                attempt,
                backoff);

            TDelayedExecutor::WaitForDuration(backoff);

            if (backoff < Config_->MaxProxyListRetryPeriod) {
                backoff *= 1.2;
            }

            if (attempt > Config_->MaxProxyListUpdateAttempts) {
                attempt = 0;
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy
