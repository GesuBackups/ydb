#include "client.h"
#include "http.h"
#include "config.h"
#include "stream.h"
#include "private.h"

#include <yt/yt/core/net/dialer.h>
#include <yt/yt/core/net/config.h>
#include <yt/yt/core/net/connection.h>

#include <yt/yt/core/concurrency/poller.h>
#include <util/string/cast.h>

namespace NYT::NHttp {

using namespace NConcurrency;
using namespace NNet;

////////////////////////////////////////////////////////////////////////////////

class TClient
    : public IClient
{
public:
    TClient(
        const TClientConfigPtr& config,
        const IDialerPtr& dialer,
        const IInvokerPtr& invoker)
        : Config_(config)
        , Dialer_(dialer)
        , Invoker_(invoker)
    { }

    TFuture<IResponsePtr> Get(
        const TString& url,
        const THeadersPtr& headers) override
    {
        return Request(EMethod::Get, url, std::nullopt, headers);
    }

    TFuture<IResponsePtr> Post(
        const TString& url,
        const TSharedRef& body,
        const THeadersPtr& headers) override
    {
        return Request(EMethod::Post, url, TSharedRef{body}, headers);
    }

    TFuture<IResponsePtr> Patch(
        const TString& url,
        const TSharedRef& body,
        const THeadersPtr& headers) override
    {
        return Request(EMethod::Patch, url, TSharedRef{body}, headers);
    }

    TFuture<IResponsePtr> Put(
        const TString& url,
        const TSharedRef& body,
        const THeadersPtr& headers) override
    {
        return Request(EMethod::Put, url, TSharedRef{body}, headers);
    }

private:
    const TClientConfigPtr Config_;
    const IDialerPtr Dialer_;
    const IInvokerPtr Invoker_;

    static int GetDefaultPort(const TUrlRef& parsedUrl)
    {
        if (parsedUrl.Protocol == "https") {
            return 443;
        } else {
            return 80;
        }
    }

    TNetworkAddress GetAddress(const TUrlRef& parsedUrl)
    {
        auto host = parsedUrl.Host;
        TNetworkAddress address;

        auto tryIP = TNetworkAddress::TryParse(host);
        if (tryIP.IsOK()) {
            address = tryIP.Value();
        } else {
            auto asyncAddress = TAddressResolver::Get()->Resolve(ToString(host));
            address = WaitFor(asyncAddress)
                .ValueOrThrow();
        }

        return TNetworkAddress(address, parsedUrl.Port.value_or(GetDefaultPort(parsedUrl)));
    }

    std::pair<THttpOutputPtr, THttpInputPtr> OpenHttp(const TNetworkAddress& address)
    {
        auto conn = WaitFor(Dialer_->Dial(address)).ValueOrThrow();
        auto input = New<THttpInput>(
            conn,
            address,
            Invoker_,
            EMessageType::Response,
            Config_);
        auto output = New<THttpOutput>(
            conn,
            EMessageType::Request,
            Config_);

        return std::make_pair(std::move(output), std::move(input));
    }

    TString SanitizeUrl(const TString& url)
    {
        // Do not expose URL parameters in error attributes.
        auto urlRef = ParseUrl(url);
        if (urlRef.PortStr.empty()) {
            return TString(urlRef.Host) + urlRef.Path;
        } else {
            return Format("%v:%v%v", urlRef.Host, urlRef.PortStr, urlRef.Path);
        }
    }

    TFuture<IResponsePtr> WrapError(const TString& url, TCallback<IResponsePtr()> action)
    {
        return BIND([=] {
            try {
                return action();
            } catch(const std::exception& ex) {
                THROW_ERROR_EXCEPTION("HTTP request failed")
                    << TErrorAttribute("url", SanitizeUrl(url))
                    << ex;
            }
        })
            .AsyncVia(Invoker_)
            .Run();
    }

    TFuture<IResponsePtr> Request(
        EMethod method,
        const TString& url,
        const std::optional<TSharedRef>& body,
        const THeadersPtr& headers)
    {
        return WrapError(url, BIND([=, this_ = MakeStrong(this)] {
            THttpOutputPtr request;
            THttpInputPtr response;

            auto urlRef = ParseUrl(url);
            auto address = GetAddress(urlRef);
            std::tie(request, response) = OpenHttp(address);

            request->SetHost(urlRef.Host, urlRef.PortStr);
            if (headers) {
                request->SetHeaders(headers);
            }

            auto requestPath = Format("%v?%v", urlRef.Path, urlRef.RawQuery);
            request->WriteRequest(method, requestPath);

            if (body) {
                WaitFor(request->Write(*body))
                    .ThrowOnError();
            }
            WaitFor(request->Close())
                .ThrowOnError();

            // Waits for response headers internally.
            response->GetStatusCode();

            return IResponsePtr(response);
        }));
    }
};

////////////////////////////////////////////////////////////////////////////////

IClientPtr CreateClient(
    const TClientConfigPtr& config,
    const IDialerPtr& dialer,
    const IInvokerPtr& invoker)
{
    return New<TClient>(config, dialer, invoker);
}

IClientPtr CreateClient(
    const TClientConfigPtr& config,
    const IPollerPtr& poller)
{
    return CreateClient(
        config,
        CreateDialer(New<TDialerConfig>(), poller, HttpLogger),
        poller->GetInvoker());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHttp
