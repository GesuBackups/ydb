#include "http_req.h"

#include <kikimr/public/sdk/cpp/client/iam/iam.h>
#include <ydb/core/grpc_caching/cached_grpc_request_actor.h>
#include <ydb/core/protos/serverless_proxy_config.pb.h>
#include <ydb/core/base/ticket_parser.h>
#include <ydb/core/tx/scheme_board/cache.h>

#include <kikimr/yndx/ycloud/api/access_service.h>
#include <kikimr/yndx/ycloud/api/iam_token_service.h>
#include <kikimr/yndx/ycloud/impl/grpc_service_cache.h>
#include <kikimr/yndx/ycloud/impl/access_service.h>
#include <kikimr/yndx/ycloud/impl/iam_token_service.h>
#include <kikimr/yndx/grpc_services/persqueue/grpc_pq_actor.h>

#include <ydb/library/http_proxy/authorization/auth_helpers.h>
#include <ydb/library/http_proxy/error/error.h>
#include <ydb/services/lib/sharding/sharding.h>

#include <library/cpp/cgiparam/cgiparam.h>
#include <library/cpp/http/misc/parsed_request.h>
#include <library/cpp/http/server/response.h>
#include <library/cpp/json/json_reader.h>
#include <library/cpp/json/json_writer.h>
#include <library/cpp/protobuf/json/json_output_create.h>
#include <library/cpp/protobuf/json/proto2json.h>
#include <library/cpp/protobuf/json/proto2json_printer.h>

#include <util/stream/file.h>
//#include <library/cpp/uri/uri.h>

namespace NKikimr::NHttpProxy {

    using namespace google::protobuf;
    using namespace Ydb::DataStreams::V1;


    class THttpAuthActor : public NActors::TActorBootstrapped<THttpAuthActor> {
    public:
        using TBase = NActors::TActorBootstrapped<THttpAuthActor>;

        THttpAuthActor(const TActorId sender, THttpRequestContext& context,
                          THolder<NKikimr::NSQS::TAwsRequestSignV4>&& signature)
            : Sender(sender)
            , Prefix(context.LogPrefix())
            , ServiceAccountId(context.ServiceAccountId)
            , ServiceAccountCredentialsProvider(context.ServiceAccountCredentialsProvider)
            , RequestId(context.RequestId)
            , Signature(std::move(signature))
            , ServiceConfig(context.ServiceConfig)
            , IamToken(context.IamToken)
            , Authorize(!context.Driver)
            , Database(context.DatabaseName)
            , StreamName(context.StreamName)
        {
        }

        TStringBuilder LogPrefix() const {
            return TStringBuilder() << Prefix << " [auth] ";
        }

    private:
        STFUNC(StateWork)
        {
            switch (ev->GetTypeRewrite()) {
                HFunc(TEvTxProxySchemeCache::TEvNavigateKeySetResult, HandleCacheNavigateResponse);
                HFunc(NCloud::TEvAccessService::TEvAuthenticateResponse, HandleAuthenticationResult);
                HFunc(NCloud::TEvIamTokenService::TEvCreateResponse, HandleServiceAccountIamToken);
                HFunc(TEvTicketParser::TEvAuthorizeTicketResult, HandleTicketParser);
                HFunc(TEvents::TEvPoisonPill, HandlePoison);
                default:
                    HandleUnexpectedEvent(ev, ctx);
                    break;
                }
        }

        void SendDescribeRequest(const TActorContext& ctx) {
            auto schemeCacheRequest = std::make_unique<NSchemeCache::TSchemeCacheNavigate>();
            NSchemeCache::TSchemeCacheNavigate::TEntry entry;
            entry.Path = NKikimr::SplitPath(Database);
            entry.Operation = NSchemeCache::TSchemeCacheNavigate::OpPath;
            entry.SyncVersion = false;
            schemeCacheRequest->ResultSet.emplace_back(entry);
            ctx.Send(MakeSchemeCacheID(), MakeHolder<TEvTxProxySchemeCache::TEvNavigateKeySet>(schemeCacheRequest.release()));
        }

        void HandleCacheNavigateResponse(TEvTxProxySchemeCache::TEvNavigateKeySetResult::TPtr& ev, const TActorContext& ctx) {
            const NSchemeCache::TSchemeCacheNavigate* navigate = ev->Get()->Request.Get();
            if (navigate->ErrorCount) {
                return ReplyWithError(ctx, NYdb::EStatus::SCHEME_ERROR, TStringBuilder() << "Database with path '" << Database << "' doesn't exists");
            }
            Y_VERIFY(navigate->ResultSet.size() == 1);
            if (navigate->ResultSet.front().PQGroupInfo) {
                const auto& description = navigate->ResultSet.front().PQGroupInfo->Description;
                FolderId = description.GetPQTabletConfig().GetYcFolderId();
                CloudId = description.GetPQTabletConfig().GetYcCloudId();
                DatabaseId = description.GetPQTabletConfig().GetYdbDatabaseId();
            }
            for (const auto& attr : navigate->ResultSet.front().Attributes) {
                if (attr.first == "folder_id") FolderId = attr.second;
                if (attr.first == "cloud_id") CloudId = attr.second;
                if (attr.first == "database_id") DatabaseId = attr.second;
            }
            SendAuthenticationRequest(ctx);
        }

        void HandlePoison(const TEvents::TEvPoisonPill::TPtr&, const TActorContext& ctx) {
            TBase::Die(ctx);
        }

        void HandleTicketParser(const TEvTicketParser::TEvAuthorizeTicketResult::TPtr& ev, const TActorContext& ctx) {

            if (ev->Get()->Error) {
                return ReplyWithError(ctx, NYdb::EStatus::UNAUTHORIZED, ev->Get()->Error.Message);
            };
            ctx.Send(Sender, new TEvServerlessProxy::TEvToken(ev->Get()->Token->GetUserSID(), "", ev->Get()->SerializedToken));

            LOG_SP_DEBUG_S(ctx, NKikimrServices::HTTP_PROXY, "Authorized successfully");

            TBase::Die(ctx);
        }

        void SendAuthenticationRequest(const TActorContext& ctx) {
            TInstant signedAt;
            if (!Signature.Get() && IamToken.empty()) {
                return ReplyWithError(ctx, NYdb::EStatus::UNAUTHORIZED, "Neither Credentials nor IAM token was provided");
            }
            if (Signature) {
                bool found = false;
                for (auto& cr : ServiceConfig.GetHttpConfig().GetYandexCloudServiceRegion()) {
                    if (cr == Signature->GetRegion()) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return ReplyWithError(ctx, NYdb::EStatus::UNAUTHORIZED, TStringBuilder() << "Wrong service region: got "
                        << Signature->GetRegion() << " expected " << ServiceConfig.GetHttpConfig().GetYandexCloudServiceRegion(0));
                }

                if (!TInstant::TryParseIso8601(Signature->GetSigningTimestamp(), signedAt)) {
                    return ReplyWithError(ctx, NYdb::EStatus::BAD_REQUEST, "Failed to parse Signature timestamp");
                }
            }


            if (Authorize) {
                auto entries = NKikimr::NGRpcProxy::GetTicketParserEntries(DatabaseId, FolderId);
                if (Signature.Get()) {
                    TEvTicketParser::TEvAuthorizeTicket::TAccessKeySignature signature;
                    signature.AccessKeyId = Signature->GetAccessKeyId();
                    signature.StringToSign = Signature->GetStringToSign();
                    signature.Signature = Signature->GetParsedSignature();
                    signature.Service = "kinesis";
                    signature.Region = Signature->GetRegion();
                    signature.SignedAt = signedAt;

                    THolder<TEvTicketParser::TEvAuthorizeTicket> request = MakeHolder<TEvTicketParser::TEvAuthorizeTicket>(std::move(signature), "", entries);
                    ctx.Send(MakeTicketParserID(), request.Release());
                } else {
                    THolder<TEvTicketParser::TEvAuthorizeTicket> request = MakeHolder<TEvTicketParser::TEvAuthorizeTicket>(IamToken, "", entries);
                    ctx.Send(MakeTicketParserID(), request.Release());

                }
                return;
            }

            THolder<NCloud::TEvAccessService::TEvAuthenticateRequest> request = MakeHolder<NCloud::TEvAccessService::TEvAuthenticateRequest>();
            request->RequestId = RequestId;

            auto& signature = *request->Request.mutable_signature();
            signature.set_access_key_id(Signature->GetAccessKeyId());
            signature.set_string_to_sign(Signature->GetStringToSign());
            signature.set_signature(Signature->GetParsedSignature());

            auto& v4params = *signature.mutable_v4_parameters();
            v4params.set_service("kinesis");
            v4params.set_region(Signature->GetRegion());

            const ui64 nanos = signedAt.NanoSeconds();
            const ui64 seconds = nanos / 1000000000ull;
            const ui64 nanos_left = nanos % 1000000000ull;

            v4params.mutable_signed_at()->set_seconds(seconds);
            v4params.mutable_signed_at()->set_nanos(nanos_left);

            ctx.Send(MakeAccessServiceID(), std::move(request));
        }

        void HandleUnexpectedEvent(const TAutoPtr<NActors::IEventHandle>& ev, const TActorContext& ctx) {
            Y_UNUSED(ev, ctx);
        }

        void HandleAuthenticationResult(NCloud::TEvAccessService::TEvAuthenticateResponse::TPtr& ev, const TActorContext& ctx) {
            if (!ev->Get()->Status.Ok()) {
                RetryCounter.Click();
                LOG_SP_INFO_S(ctx, NKikimrServices::HTTP_PROXY, "retry #" << RetryCounter.AttempN() << "; " <<
                              "can not authenticate service account user: " << ev->Get()->Status.Msg);
                if (RetryCounter.HasAttemps()) {
                    SendAuthenticationRequest(ctx);
                    return;
                }
                return ReplyWithError(ctx, NYdb::EStatus::UNAUTHORIZED, TStringBuilder() <<
                    "requestid " << RequestId << "; " <<
                    "can not authenticate service account user: " << ev->Get()->Status.Msg);
            } else if (!ev->Get()->Response.subject().has_service_account()) {
                return ReplyWithError(ctx, NYdb::EStatus::UNAUTHORIZED,
                                      "(this error should not have been reached).");
            }
            RetryCounter.Void();

            ServiceAccountId = ev->Get()->Response.subject().service_account().id();
            LOG_SP_INFO_S(ctx, NKikimrServices::HTTP_PROXY, "authenticated to " << ServiceAccountId);
            SendIamTokenRequest(ctx);
        }

        void SendIamTokenRequest(const TActorContext& ctx) {
            auto request = MakeHolder<NCloud::TEvIamTokenService::TEvCreateForServiceAccountRequest>();
            request->RequestId = RequestId;
            request->Token = ServiceAccountCredentialsProvider->GetAuthInfo();
            request->Request.set_service_account_id(ServiceAccountId);

            ctx.Send(MakeIamTokenServiceID(), std::move(request));
        }

        void ReplyWithError(const TActorContext& ctx, NYdb::EStatus status, const TString& errorText) {
            ctx.Send(Sender, new TEvServerlessProxy::TEvError(status, errorText));
            TBase::Die(ctx);
        }

        void HandleServiceAccountIamToken(NCloud::TEvIamTokenService::TEvCreateResponse::TPtr& ev, const TActorContext& ctx) {
            if (!ev->Get()->Status.Ok()) {
                RetryCounter.Click();
                LOG_SP_INFO_S(ctx, NKikimrServices::HTTP_PROXY, "retry #" << RetryCounter.AttempN() << "; " <<
                              "IAM token issue error: " << ev->Get()->Status.Msg);

                if (RetryCounter.HasAttemps()) {
                    SendIamTokenRequest(ctx);
                    return;
                }
                return ReplyWithError(ctx, NYdb::EStatus::UNAUTHORIZED, TStringBuilder() <<
                                      "IAM token issue error: " << ev->Get()->Status.Msg);
            }
            RetryCounter.Void();

            Y_VERIFY(!ev->Get()->Response.iam_token().empty());

            ctx.Send(Sender, new TEvServerlessProxy::TEvToken(ServiceAccountId, ev->Get()->Response.iam_token()));

            LOG_SP_DEBUG_S(ctx, NKikimrServices::HTTP_PROXY, "IAM token generated");

            TBase::Die(ctx);
        }

        public:
        void Bootstrap(const TActorContext& ctx) {
            TBase::Become(&THttpAuthActor::StateWork);

            if (Authorize) {
                SendDescribeRequest(ctx);
                return;
            }
            if (ServiceAccountId.empty()) {
                SendAuthenticationRequest(ctx);
            } else {
                SendIamTokenRequest(ctx);
            }
        }

    private:
        const TActorId Sender;
        const TString Prefix;
        TString ServiceAccountId;
        std::shared_ptr<NYdb::ICredentialsProvider> ServiceAccountCredentialsProvider;
        const TString RequestId;
        THolder<NKikimr::NSQS::TAwsRequestSignV4> Signature;
        TRetryCounter RetryCounter;
        const NKikimrConfig::TServerlessProxyConfig& ServiceConfig;
        TString IamToken;
        bool Authorize;
        TString FolderId;
        TString CloudId;
        TString DatabaseId;
        TString Database;
        TString StreamName;
    };


    NActors::IActor* CreateIamAuthActor(const TActorId sender, THttpRequestContext& context, THolder<NKikimr::NSQS::TAwsRequestSignV4>&& signature)
    {
        return new THttpAuthActor(sender, context, std::move(signature));
    }


    NActors::IActor* CreateIamTokenServiceActor(const NKikimrConfig::TServerlessProxyConfig& config)
    {
        NCloud::TIamTokenServiceSettings tsSettings;
        tsSettings.Endpoint = config.GetHttpConfig().GetIamTokenServiceEndpoint();
        if (config.GetCaCert()) {
            TString certificate = TFileInput(config.GetCaCert()).ReadAll();
            tsSettings.CertificateRootCA = certificate;
        }
        return NCloud::CreateIamTokenService(tsSettings);
    }

    NActors::IActor* CreateAccessServiceActor(const NKikimrConfig::TServerlessProxyConfig& config)
    {
        NCloud::TAccessServiceSettings asSettings;
        asSettings.Endpoint = config.GetHttpConfig().GetAccessServiceEndpoint();

        if (config.GetCaCert()) {
            TString certificate = TFileInput(config.GetCaCert()).ReadAll();
            asSettings.CertificateRootCA = certificate;
        }
        return NCloud::CreateAccessServiceWithCache(asSettings);
    }
} // namespace NKikimr::NHttpProxy

