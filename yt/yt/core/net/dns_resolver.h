#pragma once

#include <yt/yt/core/actions/future.h>

#include <yt/yt/core/net/address.h>

namespace NYT::NNet {

////////////////////////////////////////////////////////////////////////////////

class TDnsResolver
{
public:
    TDnsResolver(
        int retries,
        TDuration resolveTimeout,
        TDuration maxResolveTimeout,
        TDuration warningTimeout,
        std::optional<double> jitter);
    ~TDnsResolver();

    // Kindly note that returned future is set in special resolver thread
    // which does not support fibers. So please use Via/AsyncVia when
    // using this method.
    TFuture<TNetworkAddress> ResolveName(
        TString hostName,
        bool enableIPv4,
        bool enableIPv6);

private:
    class TImpl;
    const std::unique_ptr<TImpl> Impl_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNet

