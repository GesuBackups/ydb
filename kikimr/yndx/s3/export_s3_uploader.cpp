#ifndef KIKIMR_DISABLE_S3_OPS

#include "export_s3.h"

#include <ydb/core/tx/datashard/export_s3_base_uploader.h>

#include <library/cpp/actors/http/http_proxy.h>

namespace NKikimr {
namespace NYndx {

class TS3Uploader: public NDataShard::TS3UploaderBase<TS3Uploader> {
    using TS3Settings = NDataShard::TS3Settings;

    static constexpr TStringBuf S3_MDS_TESTING = "s3.mdst.yandex.net";
    static constexpr TStringBuf S3_MDS_PROD = "s3.mds.yandex.net";

    static TStringBuf NormalizeEndpoint(TStringBuf endpoint) {
        Y_UNUSED(endpoint.SkipPrefix("http://") || endpoint.SkipPrefix("https://"));
        Y_UNUSED(endpoint.ChopSuffix(":80") || endpoint.ChopSuffix(":443"));
        return endpoint;
    }

    static bool IsInternalS3(TStringBuf endpoint) {
        static const THashSet<TStringBuf> internalEndpoints = {
            S3_MDS_TESTING,
            S3_MDS_PROD,
        };

        return internalEndpoints.contains(NormalizeEndpoint(endpoint));
    }

    static bool IsInternalS3(const TS3Settings& settings) {
        return IsInternalS3(settings.Config.endpointOverride);
    }

    static TString GetProxyUrl(const TS3Settings& settings) {
        TStringBuilder url;
        switch (settings.Config.scheme) {
        case Aws::Http::Scheme::HTTP:
            url << "http://";
            break;
        case Aws::Http::Scheme::HTTPS:
            url << "https://";
            break;
        }

        url << NormalizeEndpoint(settings.Config.endpointOverride) << "/hostname";
        return url;
    }

    static void ApplyProxy(TS3Settings& settings, const TString& proxyHost, unsigned proxyPort = 0) {
        settings.Config.proxyScheme = settings.Config.scheme;
        settings.Config.proxyHost = proxyHost;
        settings.Config.proxyCaPath = settings.Config.caPath;

        if (proxyPort) {
            settings.Config.proxyPort = proxyPort;
        } else {
            switch (settings.Config.proxyScheme) {
            case Aws::Http::Scheme::HTTP:
                settings.Config.proxyPort = 4080;
                break;
            case Aws::Http::Scheme::HTTPS:
                settings.Config.proxyPort = 4480;
                break;
            }
        }
    }

    void Handle(NHttp::TEvHttpProxy::TEvHttpIncomingResponse::TPtr& ev) {
        const auto& msg = *ev->Get();

        EXPORT_LOG_D("Handle NHttp::TEvHttpProxy::TEvHttpIncomingResponse"
            << ": self# " << SelfId()
            << ", status# " << (msg.Response ? msg.Response->Status : "null")
            << ", body# " << (msg.Response ? msg.Response->Body : "null"));

        if (!msg.Response || !msg.Response->Status.StartsWith("200")) {
            EXPORT_LOG_E("Error at 'GetProxy'"
                << ": self# " << SelfId()
                << ", error# " << msg.GetError());
            return RetryOrFinish(Aws::S3::S3Error({Aws::S3::S3Errors::SERVICE_UNAVAILABLE, true}));
        }

        if (msg.Response->Body.find('<') != TStringBuf::npos) {
            EXPORT_LOG_E("Error at 'GetProxy'"
                << ": self# " << SelfId()
                << ", error# " << "invalid body"
                << ", body# " << msg.Response->Body);
            return RetryOrFinish(Aws::S3::S3Error({Aws::S3::S3Errors::SERVICE_UNAVAILABLE, true}));
        }

        ApplyProxy(Settings, TString(msg.Response->Body));
        ProxyResolved = true;

        EXPORT_LOG_N("Using proxy: "
            << (Settings.Config.proxyScheme == Aws::Http::Scheme::HTTPS ? "https://" : "http://")
            << Settings.Config.proxyHost << ":" << Settings.Config.proxyPort);

        Restart();
    }

    void PassAway() override {
        if (HttpProxy) {
            Send(HttpProxy, new TEvents::TEvPoisonPill());
        }

        TS3UploaderBase::PassAway();
    }

protected:
    bool NeedToResolveProxy() const override {
        return IsInternalS3(Settings);
    }

    void ResolveProxy() override {
        if (!HttpProxy) {
            HttpProxy = Register(NHttp::CreateHttpProxy(NMonitoring::TMetricRegistry::SharedInstance()));
        }

        Send(HttpProxy, new NHttp::TEvHttpProxy::TEvHttpOutgoingRequest(
            NHttp::THttpOutgoingRequest::CreateRequestGet(GetProxyUrl(Settings)),
            TDuration::Seconds(10)
        ));

        Become(&TThis::StateResolveProxy);
    }

public:
    using TS3UploaderBase::TS3UploaderBase;

    STATEFN(StateResolveProxy) {
        switch (ev->GetTypeRewrite()) {
            hFunc(NHttp::TEvHttpProxy::TEvHttpIncomingResponse, Handle);
        default:
            return StateBase(ev, TlsActivationContext->AsActorContext());
        }
    }

private:
    TActorId HttpProxy;

}; // TS3Uploader

IActor* TS3Export::CreateUploader(const TActorId& dataShard, ui64 txId) const {
    auto scheme = (Task.GetShardNum() == 0)
        ? GenYdbScheme(Columns, Task.GetTable())
        : Nothing();

    return new TS3Uploader(dataShard, txId, Task, std::move(scheme));
}

} // NYndx
} // NKikimr

#endif // KIKIMR_DISABLE_S3_OPS
