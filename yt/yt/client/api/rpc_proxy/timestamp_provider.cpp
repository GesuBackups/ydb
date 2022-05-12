#include "timestamp_provider.h"
#include "api_service_proxy.h"
#include "connection_impl.h"

#include <yt/yt/client/transaction_client/timestamp_provider_base.h>

namespace NYT::NApi::NRpcProxy {

using namespace NTransactionClient;
using namespace NRpc;

////////////////////////////////////////////////////////////////////////////////

class TTimestampProvider
    : public TTimestampProviderBase
{
public:
    TTimestampProvider(
        IChannelPtr channel,
        TDuration rpcTimeout,
        TDuration latestTimestampUpdatePeriod)
        : TTimestampProviderBase(latestTimestampUpdatePeriod)
        , Channel_(std::move(channel))
        , RpcTimeout_(rpcTimeout)
    { }

private:
    const IChannelPtr Channel_;
    const TDuration RpcTimeout_;

    TFuture<NTransactionClient::TTimestamp> DoGenerateTimestamps(int count) override
    {
        TApiServiceProxy proxy(Channel_);

        auto req = proxy.GenerateTimestamps();
        req->SetTimeout(RpcTimeout_);
        req->set_count(count);

        return req->Invoke().Apply(BIND([] (const TApiServiceProxy::TRspGenerateTimestampsPtr& rsp) {
            return rsp->timestamp();
        }));
    }
};

////////////////////////////////////////////////////////////////////////////////

NTransactionClient::ITimestampProviderPtr CreateTimestampProvider(
    IChannelPtr channel,
    TDuration rpcTimeout,
    TDuration latestTimestampUpdatePeriod)
{
    return New<TTimestampProvider>(
        std::move(channel),
        rpcTimeout,
        latestTimestampUpdatePeriod);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy
