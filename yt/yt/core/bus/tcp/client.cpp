#include "client.h"
#include "private.h"
#include "client.h"
#include "config.h"
#include "connection.h"

#include <yt/yt/core/bus/bus.h>

#include <yt/yt/core/concurrency/thread_affinity.h>

#include <yt/yt/core/misc/error.h>

#include <yt/yt/core/ytree/convert.h>
#include <yt/yt/core/ytree/fluent.h>

#include <errno.h>

#ifdef _unix_
    #include <netinet/tcp.h>
    #include <sys/socket.h>
#endif

namespace NYT::NBus {

using namespace NNet;
using namespace NYson;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = BusLogger;

////////////////////////////////////////////////////////////////////////////////

//! A lightweight proxy controlling the lifetime of client #TTcpConnection.
/*!
 *  When the last strong reference vanishes, it calls IBus::Terminate
 *  for the underlying connection.
 */
class TTcpClientBusProxy
    : public IBus
{
public:
    explicit TTcpClientBusProxy(TTcpConnectionPtr connection)
        : Connection_(std::move(connection))
    { }

    ~TTcpClientBusProxy()
    {
        VERIFY_THREAD_AFFINITY_ANY();
        Connection_->Terminate(TError(NBus::EErrorCode::TransportError, "Bus terminated"));
    }

    const TString& GetEndpointDescription() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return Connection_->GetEndpointDescription();
    }

    const IAttributeDictionary& GetEndpointAttributes() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return Connection_->GetEndpointAttributes();
    }

    const TString& GetEndpointAddress() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return Connection_->GetEndpointAddress();
    }

    const NNet::TNetworkAddress& GetEndpointNetworkAddress() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return Connection_->GetEndpointNetworkAddress();
    }

    TTcpDispatcherStatistics GetStatistics() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return Connection_->GetStatistics();
    }

    TFuture<void> GetReadyFuture() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return Connection_->GetReadyFuture();
    }

    TFuture<void> Send(TSharedRefArray message, const TSendOptions& options) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return Connection_->Send(std::move(message), options);
    }

    void SetTosLevel(TTosLevel tosLevel) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return Connection_->SetTosLevel(tosLevel);
    }

    void Terminate(const TError& error) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        Connection_->Terminate(error);
    }

    void SubscribeTerminated(const TCallback<void(const TError&)>& callback) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        Connection_->SubscribeTerminated(callback);
    }

    void UnsubscribeTerminated(const TCallback<void(const TError&)>& callback) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        Connection_->UnsubscribeTerminated(callback);
    }

private:
    const TTcpConnectionPtr Connection_;
};

////////////////////////////////////////////////////////////////////////////////

class TTcpBusClient
    : public IBusClient
{
public:
    explicit TTcpBusClient(TTcpBusClientConfigPtr config)
        : Config_(std::move(config))
    {
        if (Config_->Address) {
            EndpointDescription_ = *Config_->Address;
        } else if (Config_->UnixDomainSocketPath) {
            EndpointDescription_ = Format("unix://%v", *Config_->UnixDomainSocketPath);
        }

        EndpointAttributes_ = ConvertToAttributes(BuildYsonStringFluently()
            .BeginMap()
                .Item("address").Value(EndpointDescription_)
            .EndMap());
    }

    const TString& GetEndpointDescription() const override
    {
        return EndpointDescription_;
    }

    const IAttributeDictionary& GetEndpointAttributes() const override
    {
        return *EndpointAttributes_;
    }

    const TString& GetNetworkName() const override
    {
        return Config_->NetworkName
            ? Config_->NetworkName.value()
            : DefaultNetworkName;
    }

    IBusPtr CreateBus(IMessageHandlerPtr handler) override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        auto id = TConnectionId::Create();

        YT_LOG_DEBUG("Connecting to server (Address: %v, ConnectionId: %v)",
            EndpointDescription_,
            id);

        auto endpointAttributes = ConvertToAttributes(BuildYsonStringFluently()
            .BeginMap()
                .Items(*EndpointAttributes_)
                .Item("connection_id").Value(id)
            .EndMap());

        auto poller = TTcpDispatcher::TImpl::Get()->GetXferPoller();

        auto connection = New<TTcpConnection>(
            Config_,
            EConnectionType::Client,
            GetNetworkName(),
            id,
            INVALID_SOCKET,
            EndpointDescription_,
            *endpointAttributes,
            TNetworkAddress{},
            Config_->Address,
            Config_->UnixDomainSocketPath,
            std::move(handler),
            std::move(poller));
        connection->Start();

        return New<TTcpClientBusProxy>(std::move(connection));
    }

private:
    const TTcpBusClientConfigPtr Config_;

    TString EndpointDescription_;
    IAttributeDictionaryPtr EndpointAttributes_;
};

IBusClientPtr CreateTcpBusClient(TTcpBusClientConfigPtr config)
{
    return New<TTcpBusClient>(config);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NBus
