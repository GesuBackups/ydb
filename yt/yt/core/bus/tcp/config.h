#pragma once

#include "public.h"

#include <yt/yt/core/net/config.h>

#include <yt/yt/core/ytree/yson_serializable.h>

namespace NYT::NBus {

////////////////////////////////////////////////////////////////////////////////

class TTcpDispatcherConfig
    : public NYTree::TYsonSerializable
{
public:
    int ThreadPoolSize;

    //! Used for profiling export and alerts.
    std::optional<i64> NetworkBandwidth;

    TTcpDispatcherConfig();
    TTcpDispatcherConfigPtr ApplyDynamic(const TTcpDispatcherDynamicConfigPtr& dynamicConfig) const;
};

DEFINE_REFCOUNTED_TYPE(TTcpDispatcherConfig)

////////////////////////////////////////////////////////////////////////////////

class TTcpDispatcherDynamicConfig
    : public NYTree::TYsonSerializable
{
public:
    std::optional<int> ThreadPoolSize;

    TTcpDispatcherDynamicConfig();
};

DEFINE_REFCOUNTED_TYPE(TTcpDispatcherDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

class TTcpBusConfig
    : public NNet::TDialerConfig
{
public:
    bool EnableQuickAck;

    int BindRetryCount;
    TDuration BindRetryBackoff;

    TDuration ReadStallTimeout;
    TDuration WriteStallTimeout;

    bool VerifyChecksums;
    bool GenerateChecksums;

    TTcpBusConfig();
};

DEFINE_REFCOUNTED_TYPE(TTcpBusConfig)

////////////////////////////////////////////////////////////////////////////////

class TTcpBusServerConfig
    : public TTcpBusConfig
{
public:
    std::optional<int> Port;
    std::optional<TString> UnixDomainSocketPath;
    int MaxBacklogSize;
    int MaxSimultaneousConnections;
    //! "Default" network is considered when checking if the network is under heavy load.
    std::optional<TString> DefaultNetwork;

    THashMap<TString, std::vector<NNet::TIP6Network>> Networks;

    TTcpBusServerConfig();

    static TTcpBusServerConfigPtr CreateTcp(int port);
    static TTcpBusServerConfigPtr CreateUnixDomain(const TString& socketPath);
};

DEFINE_REFCOUNTED_TYPE(TTcpBusServerConfig)

////////////////////////////////////////////////////////////////////////////////

class TTcpBusClientConfig
    : public TTcpBusConfig
{
public:
    std::optional<TString> Address;
    std::optional<TString> NetworkName;
    std::optional<TString> UnixDomainSocketPath;

    TTcpBusClientConfig();

    static TTcpBusClientConfigPtr CreateTcp(const TString& address);
    static TTcpBusClientConfigPtr CreateTcp(const TString& address, const TString& network);
    static TTcpBusClientConfigPtr CreateUnixDomain(const TString& socketPath);
};

DEFINE_REFCOUNTED_TYPE(TTcpBusClientConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NBus

