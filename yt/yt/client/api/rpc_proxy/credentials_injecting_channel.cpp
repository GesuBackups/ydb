#include "credentials_injecting_channel.h"

#include <yt/yt/core/rpc/client.h>
#include <yt/yt/core/rpc/channel_detail.h>
#include <yt/yt_proto/yt/core/rpc/proto/rpc.pb.h>

namespace NYT::NApi::NRpcProxy {

using namespace NRpc;

////////////////////////////////////////////////////////////////////////////////

class TUserInjectingChannel
    : public TChannelWrapper
{
public:
    TUserInjectingChannel(IChannelPtr underlyingChannel, const std::optional<TString>& user)
        : TChannelWrapper(std::move(underlyingChannel))
        , User_(user)
    { }

    IClientRequestControlPtr Send(
        IClientRequestPtr request,
        IClientResponseHandlerPtr responseHandler,
        const TSendOptions& options) override
    {
        DoInject(request);

        return TChannelWrapper::Send(
            std::move(request),
            std::move(responseHandler),
            options);
    }

protected:
    virtual void DoInject(const IClientRequestPtr& request)
    {
        if (User_) {
            request->SetUser(*User_);
        }
    }

private:
    const std::optional<TString> User_;
};

IChannelPtr CreateUserInjectingChannel(IChannelPtr underlyingChannel, const std::optional<TString>& user)
{
    YT_VERIFY(underlyingChannel);
    return New<TUserInjectingChannel>(std::move(underlyingChannel), user);
}

////////////////////////////////////////////////////////////////////////////////

class TTokenInjectingChannel
    : public TUserInjectingChannel
{
public:
    TTokenInjectingChannel(
        IChannelPtr underlyingChannel,
        const std::optional<TString>& user,
        const TString& token)
        : TUserInjectingChannel(std::move(underlyingChannel), user)
        , Token_(token)
    { }

protected:
    void DoInject(const IClientRequestPtr& request) override
    {
        TUserInjectingChannel::DoInject(request);

        auto* ext = request->Header().MutableExtension(NRpc::NProto::TCredentialsExt::credentials_ext);
        ext->set_token(Token_);
    }

private:
    const TString Token_;
};

IChannelPtr CreateTokenInjectingChannel(
    IChannelPtr underlyingChannel,
    const std::optional<TString>& user,
    const TString& token)
{
    YT_VERIFY(underlyingChannel);
    return New<TTokenInjectingChannel>(
        std::move(underlyingChannel),
        user,
        token);
}

////////////////////////////////////////////////////////////////////////////////

class TCookieInjectingChannel
    : public TUserInjectingChannel
{
public:
    TCookieInjectingChannel(
        IChannelPtr underlyingChannel,
        const std::optional<TString>& user,
        const TString& sessionId,
        const TString& sslSessionId)
        : TUserInjectingChannel(std::move(underlyingChannel), user)
        , SessionId_(sessionId)
        , SslSessionId_(sslSessionId)
    { }

protected:
    void DoInject(const IClientRequestPtr& request) override
    {
        TUserInjectingChannel::DoInject(request);

        auto* ext = request->Header().MutableExtension(NRpc::NProto::TCredentialsExt::credentials_ext);
        ext->set_session_id(SessionId_);
        ext->set_ssl_session_id(SslSessionId_);
    }

private:
    const TString SessionId_;
    const TString SslSessionId_;
};

IChannelPtr CreateCookieInjectingChannel(
    IChannelPtr underlyingChannel,
    const std::optional<TString>& user,
    const TString& sessionId,
    const TString& sslSessionId)
{
    YT_VERIFY(underlyingChannel);
    return New<TCookieInjectingChannel>(
        std::move(underlyingChannel),
        user,
        sessionId,
        sslSessionId);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy
