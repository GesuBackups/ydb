#include <library/cpp/tvmauth/version.h>
#include <library/cpp/tvmauth/unittest.h>
#include <library/cpp/tvmauth/deprecated/service_context.h>
#include <library/cpp/actors/core/actorsystem.h>
#include <library/cpp/actors/core/log.h>
#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/hfunc.h>
#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/openssl/init/init.h>
#include <library/cpp/string_utils/base64/base64.h>
#include <ydb/core/base/appdata.h>
#include <ydb/core/base/counters.h>
#include <ydb/core/mon/mon.h>
#include <kikimr/yndx/ycloud/api/access_service.h>
#include <kikimr/yndx/ycloud/api/user_account_service.h>
#include <kikimr/yndx/ycloud/api/service_account_service.h>
#include <kikimr/yndx/ycloud/impl/access_service.h>
#include <kikimr/yndx/ycloud/impl/user_account_service.h>
#include <kikimr/yndx/ycloud/impl/service_account_service.h>
#include <kikimr/yndx/ycloud/impl/grpc_service_cache.h>
#include <ydb/library/security/util.h>
#include <util/generic/queue.h>
#include <util/generic/deque.h>
#include <util/stream/file.h>
#include <util/string/vector.h>
#include "ticket_parser.h"
#include "staff_token_builder.h"
#include "tvm_settings_updater.h"
#include "blackbox_validator.h"

namespace NKikimrYandex {

using namespace NCloud;

class TTicketParser : public TActorBootstrapped<TTicketParser> {
    using TThis = TTicketParser;
    using TBase = TActorBootstrapped<TTicketParser>;

    template <typename TRequestType>
    struct TEvRequestWithKey : TRequestType {
        TString Key;

        TEvRequestWithKey(const TString& key)
            : Key(key)
        {}
    };

    using TEvAccessServiceAuthenticateRequest = TEvRequestWithKey<NCloud::TEvAccessService::TEvAuthenticateRequest>;
    using TEvAccessServiceAuthorizeRequest = TEvRequestWithKey<NCloud::TEvAccessService::TEvAuthorizeRequest>;
    using TEvAccessServiceGetUserAccountRequest = TEvRequestWithKey<NCloud::TEvUserAccountService::TEvGetUserAccountRequest>;
    using TEvAccessServiceGetServiceAccountRequest = TEvRequestWithKey<NCloud::TEvServiceAccountService::TEvGetServiceAccountRequest>;

    NKikimrProto::TAuthConfig Config;
    TString DomainName;

    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsReceived;
    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsSuccess;
    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsErrors;
    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsErrorsRetryable;
    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsErrorsPermanent;
    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsBuiltin;
    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsTVM;
    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsBB;
    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsAS;
    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsLogin;
    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsCacheHit;
    NMonitoring::TDynamicCounters::TCounterPtr CounterTicketsCacheMiss;
    NMonitoring::THistogramPtr CounterTicketsBuildTime;

    TDuration RefreshPeriod = TDuration::Seconds(1); // how often do we check for ticket freshness/expiration
    TDuration RefreshTime = TDuration::Hours(1); // within this time we will try to refresh valid ticket
    TDuration MinErrorRefreshTime = TDuration::Seconds(1); // between this and next time we will try to refresh retryable error
    TDuration MaxErrorRefreshTime = TDuration::Minutes(1);
    TDuration LifeTime = TDuration::Hours(1); // for how long ticket will remain in the cache after last access
    TDuration ExpireTime = TDuration::Hours(24); // after what time ticket will expired and removed from cache
    TDuration TVMExpireTime = TDuration::Minutes(2); // the same but for TVM ticket

    enum class ETokenType {
        Unknown,
        Unsupported,
        Builtin,
        BlackBox,
        AccessService,
        TVM,
        Login,
    };

    ETokenType ParseTokenType(TStringBuf tokenType) {
        if (tokenType == "OAuth" || tokenType == "BlackBox") {
            if (BlackBoxValidator) {
                return ETokenType::BlackBox;
            } else {
                return ETokenType::Unsupported;
            }
        }
        if (tokenType == "Bearer" || tokenType == "IAM") {
            if (AccessServiceValidator) {
                return ETokenType::AccessService;
            } else {
                return ETokenType::Unsupported;
            }
        }
        if (tokenType == "TVM") {
            if (TVMChecker) {
                return ETokenType::TVM;
            }
            return ETokenType::Unsupported;
        }
        if (tokenType == "Login") {
            if (UseLoginProvider) {
                return ETokenType::Login;
            } else {
                return ETokenType::Unsupported;
            }
        }
        return ETokenType::Unknown;
    }

    struct TPermissionRecord {
        TString Subject;
        bool Required = false;
        yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase SubjectType;
        TEvTicketParser::TError Error;
        TStackVec<std::pair<TString, TString>> Attributes;

        bool IsPermissionReady() const {
            return !Subject.empty() || !Error.empty();
        }

        bool IsPermissionOk() const {
            return !Subject.empty();
        }

        bool IsRequired() const {
            return Required;
        }
    };

    struct TTokenRecord {
        TTokenRecord(const TTokenRecord&) = delete;
        TTokenRecord& operator =(const TTokenRecord&) = delete;

        TString Ticket;
        ui64 UID = 0; // uid from blackbox
        TString Subject; // login
        TEvTicketParser::TError Error;
        TIntrusivePtr<NACLib::TUserToken> Token;
        TString SerializedToken;
        TDeque<THolder<TEventHandle<TEvTicketParser::TEvAuthorizeTicket>>> AuthorizeRequests;
        ui64 ResponsesLeft = 0;
        TInstant InitTime;
        TInstant RefreshTime;
        TInstant ExpireTime;
        TInstant AccessTime;
        TDuration CurrentMaxRefreshTime = TDuration::Seconds(1);
        TDuration CurrentMinRefreshTime = TDuration::Seconds(1);
        ETokenType TokenType = ETokenType::Unknown;
        TString PeerName;
        TString Database;
        TStackVec<TString> AdditionalSIDs;
        THashMap<TString, TPermissionRecord> Permissions;

        NKikimr::TEvTicketParser::TEvAuthorizeTicket::TAccessKeySignature Signature;

        TString GetSubject() const {
            return Subject.empty() ? ToString(UID) : Subject;
        }

        TTokenRecord(TStringBuf ticket)
            : Ticket(ticket)
        {}

        void SetErrorRefreshTime(TTicketParser* ticketParser, TInstant now) {
            if (Error.Retryable) {
                if (CurrentMaxRefreshTime < ticketParser->MaxErrorRefreshTime) {
                    CurrentMaxRefreshTime += ticketParser->MinErrorRefreshTime;
                }
                CurrentMinRefreshTime = ticketParser->MinErrorRefreshTime;
            } else {
                CurrentMaxRefreshTime = ticketParser->RefreshTime;
                CurrentMinRefreshTime = CurrentMaxRefreshTime / 2;
            }
            SetRefreshTime(now);
        }

        void SetOkRefreshTime(TTicketParser* ticketParser, TInstant now) {
            CurrentMaxRefreshTime = ticketParser->RefreshTime;
            CurrentMinRefreshTime = CurrentMaxRefreshTime / 2;
            SetRefreshTime(now);
        }

        void SetRefreshTime(TInstant now) {
            if (CurrentMinRefreshTime < CurrentMaxRefreshTime) {
                TDuration currentDuration = CurrentMaxRefreshTime - CurrentMinRefreshTime;
                TDuration refreshDuration = CurrentMinRefreshTime + TDuration::MilliSeconds(RandomNumber<double>() * currentDuration.MilliSeconds());
                RefreshTime = now + refreshDuration;
            } else {
                RefreshTime = now + CurrentMinRefreshTime;
            }
        }

        bool IsTokenReady() const {
            return Token != nullptr;
        }

        TString GetAttributeValue(const TString& permission, const TString& key) const {
            if (auto it = Permissions.find(permission); it != Permissions.end()) {
                for (const auto& pr : it->second.Attributes) {
                    if (pr.first == key) {
                        return pr.second;
                    }
                }
            }
            return TString();
        }

        bool IsOfflineToken() const {
            switch (TokenType) {
                case ETokenType::Builtin:
                case ETokenType::TVM:
                case ETokenType::Login:
                    return true;
                default:
                    return false;
            }
        }

        TString GetAuthType() const {
            switch (TokenType) {
                case ETokenType::Unknown:
                    return "Unknown";
                case ETokenType::Unsupported:
                    return "Unsupported";
                case ETokenType::Builtin:
                    return "Builtin";
                case ETokenType::BlackBox:
                    return "BlackBox";
                case ETokenType::AccessService:
                    return "AccessService";
                case ETokenType::TVM:
                    return "TVM";
                case ETokenType::Login:
                    return "Login";
            }
        }
    };

    struct TTokenRefreshRecord {
        TString Key;
        TInstant RefreshTime;

        bool operator <(const TTokenRefreshRecord& o) const {
            return RefreshTime > o.RefreshTime;
        }
    };

    THashMap<TString, TTokenRecord> UserTokens;
    TPriorityQueue<TTokenRefreshRecord> RefreshQueue;
    TAutoPtr<NTvmAuth::TServiceContext> TVMChecker;
    TActorId BlackBoxValidator;
    TActorId AccessServiceValidator;
    TActorId StaffTokenBuilder;
    TActorId UserAccountService;
    TActorId ServiceAccountService;
    TString TvmServiceDomain;
    TString BlackBoxDomain;
    TString AccessServiceDomain;
    TString UserAccountDomain;
    TString ServiceDomain;
    std::unordered_map<TString, NLogin::TLoginProvider> LoginProviders;
    bool UseLoginProvider = false;

        static TString GetKey(TEvTicketParser::TEvAuthorizeTicket* request) {
            TStringStream key;
            if (request->Signature.AccessKeyId) {
                const auto& sign = request->Signature;
                key << sign.AccessKeyId << "-" << sign.Signature << ":" << sign.StringToSign << ":"
                    << sign.Service << ":" << sign.Region << ":" << sign.SignedAt.NanoSeconds();
            } else {
                key << request->Ticket;
            }
            key << ':';
            if (request->Database) {
                key << request->Database;
                key << ':';
            }
            for (const auto& entry : request->Entries) {
                for (auto it = entry.Attributes.begin(); it != entry.Attributes.end(); ++it) {
                    if (it != entry.Attributes.begin()) {
                        key << '-';
                    }
                    key << it->second;
                }
                key << ':';
                for (auto it = entry.Permissions.begin(); it != entry.Permissions.end(); ++it) {
                    if (it != entry.Permissions.begin()) {
                        key << '-';
                    }
                    key << it->Permission << "(" << it->Required << ")";
                }
            }
            return key.Str();
        }

    static TStringBuf GetTicketFromKey(TStringBuf key) {
        return key.Before(':');
    }

    const TString& GetPackedPublicKeys() const {
        return NTvmAuth::NUnittest::TVMKNIFE_PUBLIC_KEYS;
    }

    TInstant GetExpireTime(TInstant now) {
        return now + ExpireTime;
    }

    TInstant GetRefreshTime(TInstant now) {
        return now + RefreshTime;
    }

    TInstant GetTVMExpireTime(TInstant now) {
        return now + TVMExpireTime;
    }

    TDuration GetLifeTime() {
        return LifeTime;
    }

    static void EnrichUserTokenWithBuiltins(const TTokenRecord& tokenRecord) {
        const TString& allAuthenticatedUsers = AppData()->AllAuthenticatedUsers;
        if (!allAuthenticatedUsers.empty()) {
            tokenRecord.Token->AddGroupSID(allAuthenticatedUsers);
        }
        for (const TString& sid : tokenRecord.AdditionalSIDs) {
            tokenRecord.Token->AddGroupSID(sid);
        }
    }

    void EnrichUserTokenWithPermissions(const TTokenRecord& tokenRecord) {
        TString subject;
        TVector<TString> groups;
        for (const auto& [permission, rec] : tokenRecord.Permissions) {
            if (rec.IsPermissionOk()) {
                subject = rec.Subject;
                AddPermissionSids(groups, tokenRecord, permission);
            }
        }
        if (subject) {
            groups.emplace_back(subject);
        }
        if (tokenRecord.Subject && tokenRecord.Subject != subject) {
            groups.emplace_back(tokenRecord.Subject);
        }

        for (const TString& group : groups) {
            tokenRecord.Token->AddGroupSID(group);
        }
    }

    void InitTokenRecord(const TString& key, TTokenRecord& record, const TActorContext& ctx) {
        TInstant now = ctx.Now();
        record.InitTime = now;
        record.AccessTime = now;
        record.ExpireTime = GetExpireTime(now);
        record.RefreshTime = GetRefreshTime(now);

        if (record.Error) {
            return;
        }

        if (record.TokenType == ETokenType::Unknown || record.TokenType == ETokenType::Builtin) {
            if(record.Ticket.EndsWith("@" BUILTIN_ACL_DOMAIN)) {
                record.TokenType = ETokenType::Builtin;
                SetToken(key, record, new NACLib::TUserToken({
                    .OriginalUserToken = record.Ticket,
                    .UserSID = record.Ticket,
                    .AuthType = record.GetAuthType()
                }), ctx);
                CounterTicketsBuiltin->Inc();
                return;
            }

            if(record.Ticket.EndsWith("@" BUILTIN_ERROR_DOMAIN)) {
                record.TokenType = ETokenType::Builtin;
                SetError(key, record, {"Builtin error simulation"}, ctx);
                CounterTicketsBuiltin->Inc();
                return;
            }
        }

        if (UseLoginProvider && (record.TokenType == ETokenType::Unknown || record.TokenType == ETokenType::Login)) {
            TString database = Config.GetDomainLoginOnly() ? DomainName : record.Database;
            auto itLoginProvider = LoginProviders.find(database);
            if (itLoginProvider != LoginProviders.end()) {
                NLogin::TLoginProvider& loginProvider(itLoginProvider->second);
                auto response = loginProvider.ValidateToken({.Token = record.Ticket});
                if (response.Error) {
                    if (!response.TokenUnrecognized || record.TokenType != ETokenType::Unknown) {
                        record.TokenType = ETokenType::Login;
                        TEvTicketParser::TError error;
                        error.Message = response.Error;
                        error.Retryable = response.ErrorRetryable;
                        SetError(key, record, error, ctx);
                        CounterTicketsLogin->Inc();
                        return;
                    }
                } else {
                    record.TokenType = ETokenType::Login;
                    TVector<NACLib::TSID> groups;
                    if (response.Groups.has_value()) {
                        const std::vector<TString>& tokenGroups = response.Groups.value();
                        groups.assign(tokenGroups.begin(), tokenGroups.end());
                    } else {
                        const std::vector<TString> providerGroups = loginProvider.GetGroupsMembership(response.User);
                        groups.assign(providerGroups.begin(), providerGroups.end());
                    }
                    record.ExpireTime = ToInstant(response.ExpiresAt);
                    SetToken(key, record, new NACLib::TUserToken({
                        .OriginalUserToken = record.Ticket,
                        .UserSID = response.User,
                        .GroupSIDs = groups,
                        .AuthType = record.GetAuthType()
                    }), ctx);
                    CounterTicketsLogin->Inc();
                    return;
                }
            } else {
                if (record.TokenType == ETokenType::Login) {
                    TEvTicketParser::TError error;
                    error.Message = "Login state is not available yet";
                    error.Retryable = false;
                    SetError(key, record, error, ctx);
                    CounterTicketsLogin->Inc();
                    return;
                }
            }
        }

        if (record.TokenType == ETokenType::Unknown || record.TokenType == ETokenType::TVM) {
            if (TVMChecker) {
                NTvmAuth::TCheckedServiceTicket serviceTicket = TVMChecker->Check(record.Ticket);
                TString errorMsg = TString(NTvmAuth::StatusToString(serviceTicket.GetStatus()));
                if (serviceTicket.GetStatus() == NTvmAuth::ETicketStatus::Ok) {
                    record.ExpireTime = GetTVMExpireTime(now);
                    record.TokenType = ETokenType::TVM;
                    record.Subject = ToString<ui32>(serviceTicket.GetSrc()) + "@" + TvmServiceDomain;
                    SetToken(key, record, new NACLib::TUserToken({
                        .OriginalUserToken = record.Ticket,
                        .UserSID = record.GetSubject(),
                        .AuthType = record.GetAuthType()
                    }), ctx);
                    CounterTicketsTVM->Inc();
                    return;
                }
                if (serviceTicket.GetStatus() != NTvmAuth::ETicketStatus::Malformed) {
                    record.ExpireTime = GetTVMExpireTime(now);
                    record.TokenType = ETokenType::TVM;
                    SetError(key, record, {errorMsg}, ctx);
                    CounterTicketsTVM->Inc();
                    return;
                }
            }
        }

        if (record.TokenType == ETokenType::Unknown || record.TokenType == ETokenType::BlackBox) {
            if (BlackBoxValidator) {
                RequestBlackBoxAuthentication(key, record, ctx);
                CounterTicketsBB->Inc();
            }
        }

        if ((record.TokenType == ETokenType::Unknown && !BlackBoxValidator) || record.TokenType == ETokenType::AccessService) {
            if (AccessServiceValidator) {
                if (record.Permissions) {
                    RequestAccessServiceAuthorization(key, record, ctx);
                } else {
                    RequestAccessServiceAuthentication(key, record, ctx);
                }
                CounterTicketsAS->Inc();
            }
        }

        if (record.TokenType == ETokenType::Unknown && record.ResponsesLeft == 0) {
            record.Error.Message = "Could not find correct token validator";
            record.Error.Retryable = false;
        }
    }

    void RequestBlackBoxAuthentication(const TString& key, TTokenRecord& record, const TActorContext& ctx) const {
        record.ResponsesLeft++;
        ctx.Send(BlackBoxValidator, new TEvBlackBoxValidator::TEvValidateTicket(key, record.Ticket));
    }

    void RequestAccessServiceAuthorization(const TString& key, TTokenRecord& record, const TActorContext& ctx) const {
        for (const auto& [perm, permRecord] : record.Permissions) {
            const TString& permission(perm);
            LOG_TRACE_S(ctx, NKikimrServices::TICKET_PARSER,
                        "Ticket " << MaskTicket(record.Ticket)
                        << " asking for AccessServiceAuthorization(" << permission << ")");
            THolder<TEvAccessServiceAuthorizeRequest> request = MakeHolder<TEvAccessServiceAuthorizeRequest>(key);
            if (record.Signature.AccessKeyId) {
                const auto& sign = record.Signature;
                auto& signature = *request->Request.mutable_signature();
                signature.set_access_key_id(sign.AccessKeyId);
                signature.set_string_to_sign(sign.StringToSign);
                signature.set_signature(sign.Signature);

                auto& v4params = *signature.mutable_v4_parameters();
                v4params.set_service(sign.Service);
                v4params.set_region(sign.Region);

                v4params.mutable_signed_at()->set_seconds(sign.SignedAt.Seconds());
                v4params.mutable_signed_at()->set_nanos(sign.SignedAt.NanoSeconds() % 1000000000ull);
            } else {
                request->Request.set_iam_token(record.Ticket);
            }
            request->Request.set_permission(permission);

            if (const auto databaseId = record.GetAttributeValue(permission, "database_id"); databaseId) {
                auto* resourcePath = request->Request.add_resource_path();
                resourcePath->set_id(databaseId);
                resourcePath->set_type("ydb.database");
            } else if (const auto serviceAccountId = record.GetAttributeValue(permission, "service_account_id"); serviceAccountId) {
                auto* resourcePath = request->Request.add_resource_path();
                resourcePath->set_id(serviceAccountId);
                resourcePath->set_type("iam.serviceAccount");
            }

            if (const auto folderId = record.GetAttributeValue(permission, "folder_id"); folderId) {
                auto* resourcePath = request->Request.add_resource_path();
                resourcePath->set_id(folderId);
                resourcePath->set_type("resource-manager.folder");
            }

            record.ResponsesLeft++;
            ctx.Send(AccessServiceValidator, request.Release());
        }
    }

    void RequestAccessServiceAuthentication(const TString& key, TTokenRecord& record, const TActorContext& ctx) const {
        LOG_TRACE_S(ctx, NKikimrServices::TICKET_PARSER,
                    "Ticket " << MaskTicket(record.Ticket)
                    << " asking for AccessServiceAuthentication");
        THolder<TEvAccessServiceAuthenticateRequest> request = MakeHolder<TEvAccessServiceAuthenticateRequest>(key);
        if (record.Signature.AccessKeyId) {
            const auto& sign = record.Signature;
            auto& signature = *request->Request.mutable_signature();
            signature.set_access_key_id(sign.AccessKeyId);
            signature.set_string_to_sign(sign.StringToSign);
            signature.set_signature(sign.Signature);

            auto& v4params = *signature.mutable_v4_parameters();
            v4params.set_service(sign.Service);
            v4params.set_region(sign.Region);

            v4params.mutable_signed_at()->set_seconds(sign.SignedAt.Seconds());
            v4params.mutable_signed_at()->set_nanos(sign.SignedAt.NanoSeconds() % 1000000000ull);
        } else {
            request->Request.set_iam_token(record.Ticket);
        }
        record.ResponsesLeft++;
        ctx.Send(AccessServiceValidator, request.Release());
    }

    TString GetSubjectName(const yandex::cloud::priv::servicecontrol::v1::Subject& subject) {
        switch (subject.type_case()) {
        case yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase::kUserAccount:
            return subject.user_account().id() + "@" + AccessServiceDomain;
        case yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase::kServiceAccount:
            return subject.service_account().id() + "@" + AccessServiceDomain;
        case yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase::kAnonymousAccount:
            return "anonymous" "@" + AccessServiceDomain;
        default:
            return "Unknown subject type";
        }
    }

    static bool IsRetryableGrpcError(const NGrpc::TGrpcStatus& status) {
        bool retryable = true;
        switch (status.GRpcStatusCode) {
        case grpc::StatusCode::UNAUTHENTICATED:
        case grpc::StatusCode::PERMISSION_DENIED:
        case grpc::StatusCode::INVALID_ARGUMENT:
        case grpc::StatusCode::NOT_FOUND:
            retryable = false;
            break;
        }
        return retryable;
    }

    void SetToken(const TString& key, TTokenRecord& record, TIntrusivePtr<NACLib::TUserToken> token, const TActorContext& ctx) {
        TInstant now = ctx.Now();
        record.Error.clear();
        record.Token = token;
        EnrichUserTokenWithBuiltins(record);
        if (!record.Permissions.empty()) {
            EnrichUserTokenWithPermissions(record);
        }
        if (!token->GetUserSID().empty()) {
            record.Subject = token->GetUserSID();
        }
        record.SerializedToken = token->SerializeAsString();
        if (!record.ExpireTime) {
            record.ExpireTime = GetExpireTime(now);
        }
        if (record.IsOfflineToken()) {
            record.RefreshTime = record.ExpireTime;
        } else {
            record.SetOkRefreshTime(this, now);
        }
        CounterTicketsSuccess->Inc();
        CounterTicketsBuildTime->Collect((now - record.InitTime).MilliSeconds());
        LOG_DEBUG_S(ctx, NKikimrServices::TICKET_PARSER, "Ticket " << MaskTicket(record.Ticket) << " ("
                    << record.PeerName << ") has now valid token of " << record.Subject);
        RefreshQueue.push({key, record.RefreshTime});
    }

    void SetError(const TString& key, TTokenRecord& record, const TEvTicketParser::TError& error, const TActorContext& ctx) {
        record.Error = error;
        if (record.Error.Retryable) {
            record.ExpireTime = GetExpireTime(ctx.Now());
            record.SetErrorRefreshTime(this, ctx.Now());
            CounterTicketsErrorsRetryable->Inc();
            LOG_DEBUG_S(ctx, NKikimrServices::TICKET_PARSER, "Ticket " << MaskTicket(record.Ticket) << " ("
                        << record.PeerName << ") has now retryable error message '" << error.Message << "'");
        } else {
            record.Token = nullptr;
            record.SerializedToken.clear();
            record.SetOkRefreshTime(this, ctx.Now());
            CounterTicketsErrorsPermanent->Inc();
            LOG_DEBUG_S(ctx, NKikimrServices::TICKET_PARSER, "Ticket " << MaskTicket(record.Ticket) << " ("
                        << record.PeerName << ") has now permanent error message '" << error.Message << "'");
        }
        CounterTicketsErrors->Inc();
        RefreshQueue.push({key, record.RefreshTime});
    }

    void Respond(TTokenRecord& record, const TActorContext& ctx) {
        if (record.IsTokenReady()) {
            for (const auto& request : record.AuthorizeRequests) {
                ctx.Send(request->Sender, new TEvTicketParser::TEvAuthorizeTicketResult(record.Ticket, record.Token, record.SerializedToken), 0, request->Cookie);
            }
        } else {
            for (const auto& request : record.AuthorizeRequests) {
                ctx.Send(request->Sender, new TEvTicketParser::TEvAuthorizeTicketResult(record.Ticket, record.Error), 0, request->Cookie);
            }
        }
        record.AuthorizeRequests.clear();
    }

    bool Refresh(const TString& key, TTokenRecord& record, const TActorContext& ctx) {
        if (record.ResponsesLeft == 0 && record.AuthorizeRequests.empty()) {
            if (BlackBoxValidator && (record.TokenType == ETokenType::BlackBox || record.TokenType == ETokenType::Unknown)) {
                record.UID = 0;
                record.Subject.clear();
                record.InitTime = ctx.Now();
                RequestBlackBoxAuthentication(key, record, ctx);
                return true;
            }

            if (AccessServiceValidator && (record.TokenType == ETokenType::AccessService || record.TokenType == ETokenType::Unknown)) {
                record.UID = 0;
                record.Subject.clear();
                record.InitTime = ctx.Now();
                if (record.Permissions) {
                    RequestAccessServiceAuthorization(key, record, ctx);
                } else {
                    RequestAccessServiceAuthentication(key, record, ctx);
                }
                return true;
            }
        }
        record.RefreshTime = GetRefreshTime(ctx.Now());
        return false;
    }

    void AddPermissionSids(TVector<TString>& sids, const TTokenRecord& record, const TString& permission) {
        sids.emplace_back(permission + '@' + AccessServiceDomain);
        sids.emplace_back(permission + '-' + record.GetAttributeValue(permission, "database_id") + '@' + AccessServiceDomain);
    }

    void CrackTicket(const TString& ticketBody, TStringBuf& ticket, TStringBuf& ticketType) {
        ticket = ticketBody;
        ticketType = ticket.NextTok(' ');
        if (ticket.empty()) {
            ticket = ticketBody;
            ticketType.Clear();
        }
    }

    void Handle(TEvTicketParser::TEvAuthorizeTicket::TPtr& ev, const TActorContext& ctx) {
        TStringBuf ticket;
        TStringBuf ticketType;
        CrackTicket(ev->Get()->Ticket, ticket, ticketType);

        TString key = GetKey(ev->Get());
        TActorId sender = ev->Sender;
        ui64 cookie = ev->Cookie;

        CounterTicketsReceived->Inc();
        auto it = UserTokens.find(key);
        if (it != UserTokens.end()) {
            auto& record = it->second;
            // we know about token
            if (record.IsTokenReady()) {
                // token already have built
                record.AccessTime = ctx.Now();
                ctx.Send(sender, new TEvTicketParser::TEvAuthorizeTicketResult(ev->Get()->Ticket, record.Token, record.SerializedToken), 0, cookie);
            } else if (record.Error) {
                // token stores information about previous error
                record.AccessTime = ctx.Now();
                ctx.Send(sender, new TEvTicketParser::TEvAuthorizeTicketResult(ev->Get()->Ticket, record.Error), 0, cookie);
            } else {
                // token building in progress
                record.AuthorizeRequests.emplace_back(ev.Release());
            }
            CounterTicketsCacheHit->Inc();
            return;
        } else {
            it = UserTokens.emplace(key, ticket).first;
            CounterTicketsCacheMiss->Inc();
        }

        auto& record = it->second;
        record.Signature = ev->Get()->Signature;
        record.PeerName = std::move(ev->Get()->PeerName);
        record.Database = std::move(ev->Get()->Database);
        for (const auto& entry: ev->Get()->Entries) {
            for (const auto& permission : entry.Permissions) {
                auto& permissionRecord = record.Permissions[permission.Permission];
                permissionRecord.Attributes = entry.Attributes;
                permissionRecord.Required = permission.Required;
            }
        }

        if (ticketType) {
            record.TokenType = ParseTokenType(ticketType);
            switch (record.TokenType) {
                case ETokenType::Unsupported:
                    record.Error.Message = "Token is not supported";
                    record.Error.Retryable = false;
                    break;
                case ETokenType::Unknown:
                    record.Error.Message = "Unknown token";
                    record.Error.Retryable = false;
                    break;
                default:
                    break;
            }
        }

        if (record.Signature.AccessKeyId) {
            ticket = record.Signature.AccessKeyId;
            record.TokenType = ETokenType::AccessService;
        }

        InitTokenRecord(key, record, ctx);
        if (record.Error) {
            LOG_ERROR_S(ctx, NKikimrServices::TICKET_PARSER, "Ticket " << MaskTicket(ticket) << ": " << record.Error);
            ctx.Send(sender, new TEvTicketParser::TEvAuthorizeTicketResult(ev->Get()->Ticket, record.Error), 0, cookie);
            return;
        }
        if (record.IsTokenReady()) {
            // offline check ready
            ctx.Send(sender, new TEvTicketParser::TEvAuthorizeTicketResult(ev->Get()->Ticket, record.Token, record.SerializedToken), 0, cookie);
            return;
        }
        record.AuthorizeRequests.emplace_back(ev.Release());
    }

    void Handle(TEvBlackBoxValidator::TEvValidateTicketResult::TPtr& ev, const TActorContext& ctx) {
        TString key = ev->Get()->Key;
        auto it = UserTokens.find(key);
        if (it == UserTokens.end()) {
            // wtf? it should be there
            LOG_ERROR_S(ctx, NKikimrServices::TICKET_PARSER, "Ticket " << MaskTicket(GetTicketFromKey(key)) << " has expired during build");
        } else {
            const auto& key = it->first;
            auto& record = it->second;
            record.ResponsesLeft--;
            if (!ev->Get()->Error.empty()) {
                if (record.ResponsesLeft == 0 && (record.TokenType == ETokenType::Unknown || record.TokenType == ETokenType::BlackBox)) {
                    SetError(key, record, ev->Get()->Error, ctx);
                }
            } else {
                record.UID = ev->Get()->UID;
                record.Subject = ToString(ev->Get()->UID) + "@" + BlackBoxDomain;
                record.TokenType = ETokenType::BlackBox;
                if (StaffTokenBuilder) {
                    LOG_TRACE_S(ctx, NKikimrServices::TICKET_PARSER,
                                "Ticket " << MaskTicket(record.Ticket)
                                << " asking for StaffTokenBuilder(" << record.UID << ")");
                    record.ResponsesLeft++;
                    ctx.Send(StaffTokenBuilder, new TEvStaffTokenBuilder::TEvBuildToken(key, record.UID));
                } else {
                    SetToken(key, record, new NACLib::TUserToken({
                        .OriginalUserToken = record.Ticket,
                        .UserSID = record.Subject,
                        .AuthType = record.GetAuthType()
                    }), ctx);
                }
            }
            if (record.ResponsesLeft == 0) {
                Respond(record, ctx);
            }
        }
    }

    void Handle(TEvStaffTokenBuilder::TEvBuildTokenResult::TPtr& ev, const TActorContext& ctx) {
        auto it = UserTokens.find(ev->Get()->Key);
        if (it == UserTokens.end()) {
            // wtf? it should be there
            LOG_ERROR_S(ctx, NKikimrServices::TICKET_PARSER, "Ticket " << MaskTicket(ev->Get()->Key) << " has expired during build");
        } else {
            auto& record = it->second;
            record.ResponsesLeft--;
            if (!ev->Get()->Error.empty()) {
                SetError(ev->Get()->Key, record, ev->Get()->Error, ctx);
            } else {
                SetToken(ev->Get()->Key, record, ev->Get()->Token, ctx);
            }
            if (record.ResponsesLeft == 0) {
                Respond(record, ctx);
            }
        }
    }

    void Handle(NCloud::TEvAccessService::TEvAuthenticateResponse::TPtr& ev, const TActorContext& ctx) {
        NCloud::TEvAccessService::TEvAuthenticateResponse* response = ev->Get();
        TEvAccessServiceAuthenticateRequest* request = response->Request->Get<TEvAccessServiceAuthenticateRequest>();
        auto it = UserTokens.find(request->Key);
        if (it == UserTokens.end()) {
            // wtf? it should be there
            LOG_ERROR_S(ctx, NKikimrServices::TICKET_PARSER, "Ticket " << MaskTicket(request->Request.iam_token()) << " has expired during build");
        } else {
            const auto& key = it->first;
            auto& record = it->second;
            record.ResponsesLeft--;
            if (response->Status.Ok()) {
                switch (response->Response.subject().type_case()) {
                case yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase::kUserAccount:
                case yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase::kServiceAccount:
                case yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase::kAnonymousAccount:
                    record.Subject = GetSubjectName(response->Response.subject());
                    break;
                default:
                    record.Subject.clear();
                    SetError(key, record, {"Unknown subject type", false}, ctx);
                    break;
                }
                record.TokenType = ETokenType::AccessService;
                if (!record.Subject.empty()) {
                    switch (response->Response.subject().type_case()) {
                    case yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase::kUserAccount:
                        if (UserAccountService) {
                            LOG_TRACE_S(ctx, NKikimrServices::TICKET_PARSER,
                                        "Ticket " << MaskTicket(record.Ticket)
                                        << " asking for UserAccount(" << record.Subject << ")");
                            THolder<TEvAccessServiceGetUserAccountRequest> request = MakeHolder<TEvAccessServiceGetUserAccountRequest>(key);
                            request->Token = record.Ticket;
                            request->Request.set_user_account_id(TString(TStringBuf(record.Subject).NextTok('@')));
                            ctx.Send(UserAccountService, request.Release());
                            record.ResponsesLeft++;
                            return;
                        }
                        break;
                    case yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase::kServiceAccount:
                        if (ServiceAccountService) {
                            LOG_TRACE_S(ctx, NKikimrServices::TICKET_PARSER,
                                        "Ticket " << MaskTicket(record.Ticket)
                                        << " asking for ServiceAccount(" << record.Subject << ")");
                            THolder<TEvAccessServiceGetServiceAccountRequest> request = MakeHolder<TEvAccessServiceGetServiceAccountRequest>(key);
                            request->Token = record.Ticket;
                            request->Request.set_service_account_id(TString(TStringBuf(record.Subject).NextTok('@')));
                            ctx.Send(ServiceAccountService, request.Release());
                            record.ResponsesLeft++;
                            return;
                        }
                        break;
                    default:
                        break;
                    }
                    SetToken(key, record, new NACLib::TUserToken({
                        .OriginalUserToken = record.Ticket,
                        .UserSID = record.Subject,
                        .AuthType = record.GetAuthType()
                    }), ctx);
                }
            } else {
                if (record.ResponsesLeft == 0 && (record.TokenType == ETokenType::Unknown || record.TokenType == ETokenType::AccessService)) {
                    bool retryable = IsRetryableGrpcError(response->Status);
                    SetError(key, record, {response->Status.Msg, retryable}, ctx);
                }
            }
            if (record.ResponsesLeft == 0) {
                Respond(record, ctx);
            }
        }
    }

    void Handle(NCloud::TEvUserAccountService::TEvGetUserAccountResponse::TPtr& ev, const TActorContext& ctx) {
        TEvAccessServiceGetUserAccountRequest* request = ev->Get()->Request->Get<TEvAccessServiceGetUserAccountRequest>();
        auto it = UserTokens.find(request->Key);
        if (it == UserTokens.end()) {
            // wtf? it should be there
            LOG_ERROR_S(ctx, NKikimrServices::TICKET_PARSER, "Ticket has expired during build (TEvGetUserAccountResponse)");
        } else {
            const auto& key = it->first;
            auto& record = it->second;
            record.ResponsesLeft--;
            if (!ev->Get()->Status.Ok()) {
                SetError(key, record, {ev->Get()->Status.Msg}, ctx);
            } else {
                if (StaffTokenBuilder) {
                    LOG_TRACE_S(ctx, NKikimrServices::TICKET_PARSER,
                                "Ticket " << MaskTicket(record.Ticket)
                                << " asking for StaffTokenBuilder(" << ev->Get()->Response.yandex_passport_user_account().login() << ")");
                    record.ResponsesLeft++;
                    ctx.Send(StaffTokenBuilder, new TEvStaffTokenBuilder::TEvBuildToken(key, ev->Get()->Response.yandex_passport_user_account().login()));
                } else {
                    SetToken(key, record, new NACLib::TUserToken({
                        .OriginalUserToken = record.Ticket,
                        .UserSID = ev->Get()->Response.yandex_passport_user_account().login() + "@" + UserAccountDomain,
                        .AuthType = record.GetAuthType()
                    }), ctx);
                }
            }
            if (record.ResponsesLeft == 0) {
                Respond(record, ctx);
            }
        }
    }

    void Handle(NCloud::TEvServiceAccountService::TEvGetServiceAccountResponse::TPtr& ev, const TActorContext& ctx) {
        TEvAccessServiceGetServiceAccountRequest* request = ev->Get()->Request->Get<TEvAccessServiceGetServiceAccountRequest>();
        auto it = UserTokens.find(request->Key);
        if (it == UserTokens.end()) {
            // wtf? it should be there
            LOG_ERROR_S(ctx, NKikimrServices::TICKET_PARSER, "Ticket has expired during build (TEvGetServiceAccountResponse)");
        } else {
            const auto& key = it->first;
            auto& record = it->second;
            record.ResponsesLeft--;
            if (!ev->Get()->Status.Ok()) {
                SetError(key, record, {ev->Get()->Status.Msg}, ctx);
            } else {
                SetToken(key, record, new NACLib::TUserToken(record.Ticket, ev->Get()->Response.name() + "@" + ServiceDomain, {}), ctx);
            }
            if (record.ResponsesLeft == 0) {
                Respond(record, ctx);
            }
        }
    }

    void Handle(NCloud::TEvAccessService::TEvAuthorizeResponse::TPtr& ev, const TActorContext& ctx) {
        NCloud::TEvAccessService::TEvAuthorizeResponse* response = ev->Get();
        TEvAccessServiceAuthorizeRequest* request = response->Request->Get<TEvAccessServiceAuthorizeRequest>();
        const TString& key(request->Key);
        auto itToken = UserTokens.find(key);
        if (itToken == UserTokens.end()) {
            LOG_ERROR_S(ctx, NKikimrServices::TICKET_PARSER,
                        "Ticket(key) "
                        << MaskTicket(key)
                        << " has expired during permission check");
        } else {
            TTokenRecord& record = itToken->second;
            TString permission = request->Request.permission();
            TString subject;
            yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase subjectType = yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase::TYPE_NOT_SET;
            auto itPermission = record.Permissions.find(permission);
            if (itPermission != record.Permissions.end()) {
                if (response->Status.Ok()) {
                    subject = GetSubjectName(response->Response.subject());
                    subjectType = response->Response.subject().type_case();
                    itPermission->second.Subject = subject;
                    itPermission->second.SubjectType = subjectType;
                    itPermission->second.Error.clear();
                    LOG_TRACE_S(ctx, NKikimrServices::TICKET_PARSER,
                                "Ticket "
                                << MaskTicket(record.Ticket)
                                << " permission "
                                << permission
                                << " now has a valid subject \""
                                << subject
                                << "\"");
                } else {
                    bool retryable = IsRetryableGrpcError(response->Status);
                    itPermission->second.Error = {response->Status.Msg, retryable};
                    if (itPermission->second.Subject.empty() || !retryable) {
                        itPermission->second.Subject.clear();
                        LOG_TRACE_S(ctx, NKikimrServices::TICKET_PARSER,
                                    "Ticket "
                                    << MaskTicket(record.Ticket)
                                    << " permission "
                                    << permission
                                    << " now has a permanent error \""
                                    << itPermission->second.Error
                                    << "\" "
                                    << " retryable:"
                                    << retryable);
                    } else if (retryable) {
                        LOG_TRACE_S(ctx, NKikimrServices::TICKET_PARSER,
                                    "Ticket "
                                    << MaskTicket(record.Ticket)
                                    << " permission "
                                    << permission
                                    << " now has a retryable error \""
                                    << response->Status.Msg
                                    << "\"");
                    }
                }
            } else {
                LOG_WARN_S(ctx, NKikimrServices::TICKET_PARSER, "Received response for unknown permission " << permission << " for ticket " << MaskTicket(record.Ticket));
            }
            if (--record.ResponsesLeft == 0) {
                ui32 permissionsOk = 0;
                ui32 retryableErrors = 0;
                bool requiredPermissionFailed = false;
                TEvTicketParser::TError error;
                for (const auto& [permission, rec] : record.Permissions) {
                    if (rec.IsPermissionOk()) {
                        ++permissionsOk;
                        if (subject.empty()) {
                            subject = rec.Subject;
                            subjectType = rec.SubjectType;
                        }
                    } else if (rec.IsRequired()) {
                        TString id;
                        if (TString folderId = record.GetAttributeValue(permission, "folder_id")) {
                            id += "folder_id " + folderId;
                        } else if (TString serviceAccountId = record.GetAttributeValue(permission, "service_account_id")) {
                            id += "service_account_id " + serviceAccountId;
                        }
                        error = rec.Error;
                        error.Message = permission + " for " + id + " - " + error.Message;
                        requiredPermissionFailed = true;
                        break;
                    } else {
                        if (rec.Error.Retryable) {
                            ++retryableErrors;
                            error = rec.Error;
                            break;
                        } else if (!error) {
                            error = rec.Error;
                        }
                    }
                }
                if (permissionsOk > 0 && retryableErrors == 0 && !requiredPermissionFailed) {
                    record.TokenType = ETokenType::AccessService;
                    switch (subjectType) {
                    case yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase::kUserAccount:
                        if (UserAccountService) {
                            LOG_TRACE_S(ctx, NKikimrServices::TICKET_PARSER,
                                        "Ticket " << MaskTicket(record.Ticket)
                                        << " asking for UserAccount(" << subject << ")");
                            THolder<TEvAccessServiceGetUserAccountRequest> request = MakeHolder<TEvAccessServiceGetUserAccountRequest>(key);
                            request->Token = record.Ticket;
                            request->Request.set_user_account_id(TString(TStringBuf(subject).NextTok('@')));
                            ctx.Send(UserAccountService, request.Release());
                            record.ResponsesLeft++;
                            return;
                        }
                        break;
                    case yandex::cloud::priv::servicecontrol::v1::Subject::TypeCase::kServiceAccount:
                        if (ServiceAccountService) {
                            LOG_TRACE_S(ctx, NKikimrServices::TICKET_PARSER,
                                        "Ticket " << MaskTicket(record.Ticket)
                                        << " asking for ServiceAccount(" << subject << ")");
                            THolder<TEvAccessServiceGetServiceAccountRequest> request = MakeHolder<TEvAccessServiceGetServiceAccountRequest>(key);
                            request->Token = record.Ticket;
                            request->Request.set_service_account_id(TString(TStringBuf(subject).NextTok('@')));
                            ctx.Send(ServiceAccountService, request.Release());
                            record.ResponsesLeft++;
                            return;
                        }
                        break;
                    default:
                        break;
                    }
                    SetToken(request->Key, record, new NACLib::TUserToken(record.Ticket, subject, {}), ctx);
                } else if (record.ResponsesLeft == 0 && (record.TokenType == ETokenType::Unknown || record.TokenType == ETokenType::AccessService)) {
                    SetError(request->Key, record, error, ctx);
                }
            }
            if (record.ResponsesLeft == 0) {
                Respond(record, ctx);
            }
        }
    }

    void Handle(TEvTicketParser::TEvRefreshTicket::TPtr& ev, const TActorContext& ctx) {
        const TString& ticket(ev->Get()->Ticket);
        auto it = UserTokens.find(ticket);
        if (it != UserTokens.end()) {
            Refresh(ticket, it->second, ctx);
        }
    }

    void Handle(TEvTicketParser::TEvDiscardTicket::TPtr& ev, const TActorContext&) {
        UserTokens.erase(ev->Get()->Ticket);
    }

    static TString GetLoginProviderKeys(const NLogin::TLoginProvider& loginProvider) {
        TStringBuilder keys;
        for (const auto& [key, pubKey, privKey, expiresAt] : loginProvider.Keys) {
            if (!keys.empty()) {
                keys << ",";
            }
            keys << key;
        }
        return keys;
    }

    void Handle(TEvTicketParser::TEvUpdateLoginSecurityState::TPtr& ev, const TActorContext& ctx) {
        auto& loginProvider = LoginProviders[ev->Get()->SecurityState.GetAudience()];
        loginProvider.UpdateSecurityState(ev->Get()->SecurityState);
        LOG_DEBUG_S(ctx, NKikimrServices::TICKET_PARSER,
            "Updated state for " << loginProvider.Audience << " keys " << GetLoginProviderKeys(loginProvider));
    }

    void HandleRefresh(const NActors::TActorContext& ctx) {
        while (!RefreshQueue.empty() && RefreshQueue.top().RefreshTime <= ctx.Now()) {
            TString key = RefreshQueue.top().Key;
            RefreshQueue.pop();
            auto itToken = UserTokens.find(key);
            if (itToken == UserTokens.end()) {
                continue;
            }
            auto& record(itToken->second);
            if ((record.ExpireTime > ctx.Now()) && (record.AccessTime + GetLifeTime() > ctx.Now())) {
                LOG_DEBUG_S(ctx, NKikimrServices::TICKET_PARSER, "Refreshing ticket " << MaskTicket(record.Ticket));
                if (!Refresh(key, record, ctx)) {
                    RefreshQueue.push({key, record.RefreshTime});
                }
            } else {
                LOG_DEBUG_S(ctx, NKikimrServices::TICKET_PARSER, "Expired ticket " << MaskTicket(record.Ticket));
                if (!record.AuthorizeRequests.empty()) {
                    record.Error = {"Timed out", true};
                    Respond(record, ctx);
                }
                UserTokens.erase(itToken);
            }
        }
        ctx.Schedule(RefreshPeriod, new NActors::TEvents::TEvWakeup());
    }

    void Handle(TEvTVMSettingsUpdater::TEvUpdateTVMSettings::TPtr& ev, const TActorContext& ctx) {
        RecreateTVMChecker(ev->Get()->ServiceTVMId, ev->Get()->PublicKeys, ctx);
    }

    void RecreateTVMChecker(const ui32 serviceTVMId, const TString& publicKeys, const TActorContext& ctx) {
       try {
            TString keys = publicKeys;
            if (keys.empty()) {
                keys = GetPackedPublicKeys();
            }

            TVMChecker = new NTvmAuth::TServiceContext(NTvmAuth::TServiceContext::CheckingFactory(serviceTVMId, keys));
        } catch (const std::exception& e) {
            LOG_ERROR_S(ctx, NKikimrServices::TICKET_PARSER, TStringBuilder() << "Exception while updating TVM public keys: " << e.what());
        }
    }

    static TStringBuf HtmlBool(bool v) {
        return v ? "<span style='font-weight:bold'>&#x2611;</span>" : "<span style='font-weight:bold'>&#x2610;</span>";
    }

    void Handle(NMon::TEvHttpInfo::TPtr& ev, const TActorContext& ctx) {
        const auto& params = ev->Get()->Request.GetParams();
        TStringBuilder html;
        if (params.Has("token")) {
            TString token = params.Get("token");
            html << "<head>";
            html << "<style>";
            html << "table.ticket-parser-proplist > tbody > tr > td { padding: 1px 3px; } ";
            html << "table.ticket-parser-proplist > tbody > tr > td:first-child { font-weight: bold; text-align: right; } ";
            html << "</style>";
            for (const std::pair<const TString, TTokenRecord>& pr : UserTokens) {
                if (MD5::Calc(pr.first) == token) {
                    const TTokenRecord& record = pr.second;
                    html << "<div>";
                    html << "<table class='ticket-parser-proplist'>";
                    html << "<tr><td>Ticket</td><td>" << MaskTicket(record.Ticket) << "</td></tr>";
                    if (record.TokenType == ETokenType::Login) {
                        TVector<TString> tokenData;
                        Split(record.Ticket, ".", tokenData);
                        if (tokenData.size() > 1) {
                            TString header;
                            TString payload;
                            try {
                                header = Base64DecodeUneven(tokenData[0]);
                            }
                            catch (const std::exception&) {
                                header = tokenData[0];
                            }
                            try {
                                payload = Base64DecodeUneven(tokenData[1]);
                            }
                            catch (const std::exception&) {
                                payload = tokenData[1];
                            }
                            html << "<tr><td>Header</td><td>" << header << "</td></tr>";
                            html << "<tr><td>Payload</td><td>" << payload << "</td></tr>";
                        }
                    }
                    html << "<tr><td>UID</td><td>" << record.UID << "</td></tr>";
                    html << "<tr><td>Database</td><td>" << record.Database << "</td></tr>";
                    html << "<tr><td>Subject</td><td>" << record.Subject << "</td></tr>";
                    html << "<tr><td>Additional SIDs</td><td>" << JoinStrings(record.AdditionalSIDs.begin(), record.AdditionalSIDs.end(), ", ") << "</td></tr>";
                    html << "<tr><td>Error</td><td>" << record.Error << "</td></tr>";
                    html << "<tr><td>Requests Infly</td><td>" << record.AuthorizeRequests.size() << "</td></tr>";
                    html << "<tr><td>Responses Left</td><td>" << record.ResponsesLeft << "</td></tr>";
                    html << "<tr><td>Refresh Time</td><td>" << record.RefreshTime << "</td></tr>";
                    html << "<tr><td>Expire Time</td><td>" << record.ExpireTime << "</td></tr>";
                    html << "<tr><td>Access Time</td><td>" << record.AccessTime << "</td></tr>";
                    html << "<tr><td>Peer Name</td><td>" << record.PeerName << "</td></tr>";
                    if (record.Token != nullptr) {
                        html << "<tr><td>User SID</td><td>" << record.Token->GetUserSID() << "</td></tr>";
                        for (const TString& group : record.Token->GetGroupSIDs()) {
                            html << "<tr><td>Group SID</td><td>" << group << "</td></tr>";
                        }
                    }

                    for (const auto& [permissionName, perm] : record.Permissions) {
                        html << "<tr><td></td><td></td></tr>";
                        html << "<tr><td>Permission \"" << permissionName << (perm.Required ? " (required)" : "") << "\"</td><td>" << (perm.Error ? "Error:" + perm.Error.ToString() : perm.Subject) << "</td></tr>";
                        for (const auto& [attributeName, value] : perm.Attributes) {
                            html << "<tr><td>Attribute \"" << attributeName << "\"</td><td>" << value << "</td></tr>";
                        }
                    }
                    html << "</table>";
                    html << "</div>";
                    break;
                }
            }
            html << "</head>";
        } else {
            html << "<head>";
            html << "<script>$('.container').css('width', 'auto');</script>";
            html << "<style>";
            html << "table.ticket-parser-proplist > tbody > tr > td { padding: 1px 3px; } ";
            html << "table.ticket-parser-proplist > tbody > tr > td:first-child { font-weight: bold; text-align: right; } ";
            html << "table.simple-table1 th { margin: 0px 3px; text-align: center; } ";
            html << "table.simple-table1 > tbody > tr > td:nth-child(2) { text-align: right; }";
            html << "table.simple-table1 > tbody > tr > td:nth-child(7) { text-align: right; }";
            html << "table.simple-table1 > tbody > tr > td:nth-child(8) { text-align: right; }";
            html << "table.simple-table1 > tbody > tr > td:nth-child(9) { white-space: nowrap; }";
            html << "table.simple-table1 > tbody > tr > td:nth-child(10) { white-space: nowrap; }";
            html << "table.simple-table1 > tbody > tr > td:nth-child(11) { white-space: nowrap; }";
            html << "table.simple-table1 > tbody > tr > td:nth-child(12) { white-space: nowrap; }";
            html << "table.table-hover tbody tr:hover > td { background-color: #9dddf2; }";
            html << "</style>";
            html << "</head>";
            html << "<div style='margin-bottom: 10px; margin-left: 100px'>";
            html << "<table class='ticket-parser-proplist'>";
            html << "<tr><td>User Tokens</td><td>" << UserTokens.size() << "</td></tr>";
            html << "<tr><td>Refresh Queue</td><td>" << RefreshQueue.size() << "</td></tr>";
            if (!RefreshQueue.empty()) {
                html << "<tr><td>Refresh Queue Time</td><td>" << RefreshQueue.top().RefreshTime << "</td></tr>";
            }
            html << "<tr><td>Refresh Period</td><td>" << RefreshPeriod << "</td></tr>";
            html << "<tr><td>Refresh Time</td><td>" << RefreshTime << "</td></tr>";
            html << "<tr><td>Min Error Refresh Time</td><td>" << MinErrorRefreshTime << "</td></tr>";
            html << "<tr><td>Max Error Refresh Time</td><td>" << MaxErrorRefreshTime << "</td></tr>";
            html << "<tr><td>Life Time</td><td>" << LifeTime << "</td></tr>";
            html << "<tr><td>Expire Time</td><td>" << ExpireTime << "</td></tr>";
            if (UseLoginProvider) {
                for (const auto& [databaseName, loginProvider] : LoginProviders) {
                    html << "<tr><td>LoginProvider Database</td><td>" << databaseName << " (" << loginProvider.Audience << ")</td></tr>";
                    html << "<tr><td>LoginProvider Keys</td><td>" << GetLoginProviderKeys(loginProvider) << "</td></tr>";
                    html << "<tr><td>LoginProvider Sids</td><td>" << loginProvider.Sids.size() << "</td></tr>";
                }
            }
            html << "<tr><td>TVM</td><td>" << HtmlBool(TVMChecker != nullptr) << "</td></tr>";
            html << "<tr><td>Login</td><td>" << HtmlBool(UseLoginProvider) << "</td></tr>";
            html << "<tr><td>Black Box</td><td>" << HtmlBool((bool)BlackBoxValidator) << "</td></tr>";
            html << "<tr><td>Access Service</td><td>" << HtmlBool((bool)AccessServiceValidator) << "</td></tr>";
            html << "<tr><td>Staff Token Builder</td><td>" << HtmlBool((bool)StaffTokenBuilder) << "</td></tr>";
            html << "<tr><td>User Account Service</td><td>" << HtmlBool((bool)UserAccountService) << "</td></tr>";
            html << "<tr><td>Service Account Service</td><td>" << HtmlBool((bool)ServiceAccountService) << "</td></tr>";
            html << "</table>";
            html << "</div>";

            html << "<div>";
            html << "<table class='table simple-table1 table-hover table-condensed'>";
            html << "<thead><tr>";
            html << "<th>Ticket</th>";
            html << "<th>UID</th>";
            html << "<th>Database</th>";
            html << "<th>Subject</th>";
            html << "<th>Error</th>";
            html << "<th>Token</th>";
            html << "<th>Requests</th>";
            html << "<th>Responses Left</th>";
            html << "<th>Refresh</th>";
            html << "<th>Expire</th>";
            html << "<th>Access</th>";
            html << "<th>Peer</th>";
            html << "</tr></thead><tbody>";
            for (const std::pair<const TString, TTokenRecord>& pr : UserTokens) {
                const TTokenRecord& record = pr.second;
                html << "<tr>";
                html << "<td>" << MaskTicket(record.Ticket) << "</td>";
                html << "<td>" << record.UID << "</td>";
                html << "<td>" << record.Database << "</td>";
                html << "<td>" << record.Subject << "</td>";
                html << "<td>" << record.Error << "</td>";
                html << "<td>" << "<a href='ticket_parser?token=" << MD5::Calc(pr.first) << "'>" << HtmlBool(record.Token != nullptr) << "</a>" << "</td>";
                html << "<td>" << record.AuthorizeRequests.size() << "</td>";
                html << "<td>" << record.ResponsesLeft << "</td>";
                html << "<td>" << record.RefreshTime << "</td>";
                html << "<td>" << record.ExpireTime << "</td>";
                html << "<td>" << record.AccessTime << "</td>";
                html << "<td>" << record.PeerName << "</td>";
                html << "</tr>";
            }
            html << "</tbody></table>";
            html << "</div>";
        }
        ctx.Send(ev->Sender, new NMon::TEvHttpInfoRes(html));
    }

    void Die(const TActorContext& ctx) override {
        if (BlackBoxValidator) {
            ctx.Send(BlackBoxValidator, new TEvents::TEvPoisonPill);
        }
        if (AccessServiceValidator) {
            ctx.Send(AccessServiceValidator, new TEvents::TEvPoisonPill);
        }
        if (StaffTokenBuilder) {
            ctx.Send(StaffTokenBuilder, new TEvents::TEvPoisonPill);
        }
        if (UserAccountService) {
            ctx.Send(UserAccountService, new TEvents::TEvPoisonPill);
        }
        if (ServiceAccountService) {
            ctx.Send(ServiceAccountService, new TEvents::TEvPoisonPill);
        }
        TBase::Die(ctx);
    }

public:
    static constexpr NKikimrServices::TActivity::EType ActorActivityType() { return NKikimrServices::TActivity::TICKET_PARSER_ACTOR; }

    void StateWork(TAutoPtr<NActors::IEventHandle>& ev, const NActors::TActorContext& ctx) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvTicketParser::TEvAuthorizeTicket, Handle);
            HFunc(TEvTicketParser::TEvRefreshTicket, Handle);
            HFunc(TEvTicketParser::TEvDiscardTicket, Handle);
            HFunc(TEvTicketParser::TEvUpdateLoginSecurityState, Handle);
            HFunc(TEvStaffTokenBuilder::TEvBuildTokenResult, Handle);
            HFunc(TEvBlackBoxValidator::TEvValidateTicketResult, Handle);
            HFunc(TEvTVMSettingsUpdater::TEvUpdateTVMSettings, Handle);
            HFunc(NCloud::TEvAccessService::TEvAuthenticateResponse, Handle);
            HFunc(NCloud::TEvAccessService::TEvAuthorizeResponse, Handle);
            HFunc(NCloud::TEvUserAccountService::TEvGetUserAccountResponse, Handle);
            HFunc(NCloud::TEvServiceAccountService::TEvGetServiceAccountResponse, Handle);
            HFunc(NMon::TEvHttpInfo, Handle);
            CFunc(TEvents::TSystem::Wakeup, HandleRefresh);
            CFunc(TEvents::TSystem::PoisonPill, Die);
        }
    }

    void Bootstrap(const TActorContext& ctx) {
        TIntrusivePtr<NMonitoring::TDynamicCounters> rootCounters = AppData(ctx)->Counters;
        TIntrusivePtr<NMonitoring::TDynamicCounters> authCounters = GetServiceCounters(rootCounters, "auth");
        NMonitoring::TDynamicCounterPtr counters = authCounters->GetSubgroup("subsystem", "TicketParser");
        CounterTicketsReceived = counters->GetCounter("TicketsReceived", true);
        CounterTicketsSuccess = counters->GetCounter("TicketsSuccess", true);
        CounterTicketsErrors = counters->GetCounter("TicketsErrors", true);
        CounterTicketsErrorsRetryable = counters->GetCounter("TicketsErrorsRetryable", true);
        CounterTicketsErrorsPermanent = counters->GetCounter("TicketsErrorsPermanent", true);
        CounterTicketsBuiltin = counters->GetCounter("TicketsBuiltin", true);
        CounterTicketsTVM = counters->GetCounter("TicketsTVM", true);
        CounterTicketsBB = counters->GetCounter("TicketsBB", true);
        CounterTicketsAS = counters->GetCounter("TicketsAS", true);
        CounterTicketsLogin = counters->GetCounter("TicketsLogin", true);
        CounterTicketsCacheHit = counters->GetCounter("TicketsCacheHit", true);
        CounterTicketsCacheMiss = counters->GetCounter("TicketsCacheMiss", true);
        CounterTicketsBuildTime = counters->GetHistogram("TicketsBuildTimeMs",
                                                         NMonitoring::ExplicitHistogram({0, 1, 5, 10, 50, 100, 500, 1000, 2000, 5000, 10000, 30000, 60000}));

        TvmServiceDomain = Config.GetTvmServiceDomain();
        BlackBoxDomain = Config.GetBlackBoxDomain();
        AccessServiceDomain = Config.GetAccessServiceDomain();
        UserAccountDomain = Config.GetUserAccountDomain();
        ServiceDomain = Config.GetServiceDomain();

        if (Config.GetUseBlackBox()) {
            BlackBoxValidator = ctx.Register(CreateBlackBoxValidator(Config.GetBlackBoxEndpoint()), TMailboxType::Simple, AppData(ctx)->IOPoolId);
        }
        if (Config.GetUseAccessService()) {
            NCloud::TAccessServiceSettings settings;
            settings.Endpoint = Config.GetAccessServiceEndpoint();
            if (Config.GetUseAccessServiceTLS()) {
                settings.CertificateRootCA = TUnbufferedFileInput(Config.GetPathToRootCA()).ReadAll();
            }
            settings.GrpcKeepAliveTimeMs = Config.GetAccessServiceGrpcKeepAliveTimeMs();
            settings.GrpcKeepAliveTimeoutMs = Config.GetAccessServiceGrpcKeepAliveTimeoutMs();
            AccessServiceValidator = ctx.Register(NCloud::CreateAccessService(settings), TMailboxType::Simple, AppData(ctx)->IOPoolId);
            if (Config.GetCacheAccessServiceAuthentication()) {
                AccessServiceValidator = ctx.Register(CreateGrpcServiceCache<TEvAccessService::TEvAuthenticateRequest, TEvAccessService::TEvAuthenticateResponse>(
                                                          AccessServiceValidator,
                                                          Config.GetGrpcCacheSize(),
                                                          TDuration::MilliSeconds(Config.GetGrpcSuccessLifeTime()),
                                                          TDuration::MilliSeconds(Config.GetGrpcErrorLifeTime())), TMailboxType::Simple, AppData(ctx)->UserPoolId);
            }
            if (Config.GetCacheAccessServiceAuthorization()) {
                AccessServiceValidator = ctx.Register(CreateGrpcServiceCache<TEvAccessService::TEvAuthorizeRequest, TEvAccessService::TEvAuthorizeResponse>(
                                                          AccessServiceValidator,
                                                          Config.GetGrpcCacheSize(),
                                                          TDuration::MilliSeconds(Config.GetGrpcSuccessLifeTime()),
                                                          TDuration::MilliSeconds(Config.GetGrpcErrorLifeTime())), TMailboxType::Simple, AppData(ctx)->UserPoolId);
            }
        }

        if (Config.GetUseStaff()) {
            StaffTokenBuilder = ctx.Register(CreateStaffTokenBuilder(Config.GetStaffApiUserToken(), Config.GetStaffEndpoint()), TMailboxType::Simple, AppData(ctx)->IOPoolId);
        }

        if (Config.GetUseUserAccountService()) {
            TUserAccountServiceSettings settings;
            settings.Endpoint = Config.GetUserAccountServiceEndpoint();
            if (Config.GetUseUserAccountServiceTLS()) {
                settings.CertificateRootCA = TUnbufferedFileInput(Config.GetPathToRootCA()).ReadAll();
            }
            UserAccountService = ctx.Register(CreateUserAccountService(settings), TMailboxType::Simple, AppData(ctx)->IOPoolId);
            if (Config.GetCacheUserAccountService()) {
                UserAccountService = ctx.Register(CreateGrpcServiceCache<TEvUserAccountService::TEvGetUserAccountRequest, TEvUserAccountService::TEvGetUserAccountResponse>(
                                                      UserAccountService,
                                                      Config.GetGrpcCacheSize(),
                                                      TDuration::MilliSeconds(Config.GetGrpcSuccessLifeTime()),
                                                      TDuration::MilliSeconds(Config.GetGrpcErrorLifeTime())), TMailboxType::Simple, AppData(ctx)->UserPoolId);
            }
        }

        if (Config.GetUseServiceAccountService()) {
            NCloud::TServiceAccountServiceSettings settings;
            settings.Endpoint = Config.GetServiceAccountServiceEndpoint();
            if (Config.GetUseServiceAccountServiceTLS()) {
                settings.CertificateRootCA = TUnbufferedFileInput(Config.GetPathToRootCA()).ReadAll();
            }
            ServiceAccountService = ctx.Register(NCloud::CreateServiceAccountService(settings), TMailboxType::Simple, AppData(ctx)->IOPoolId);
            if (Config.GetCacheServiceAccountService()) {
                ServiceAccountService = ctx.Register(CreateGrpcServiceCache<TEvServiceAccountService::TEvGetServiceAccountRequest, TEvServiceAccountService::TEvGetServiceAccountResponse>(
                                                         ServiceAccountService,
                                                         Config.GetGrpcCacheSize(),
                                                         TDuration::MilliSeconds(Config.GetGrpcSuccessLifeTime()),
                                                         TDuration::MilliSeconds(Config.GetGrpcErrorLifeTime())), TMailboxType::Simple, AppData(ctx)->UserPoolId);
            }
        }

        if (Config.HasTVMConfig() && Config.GetTVMConfig().HasEnabled() && Config.GetTVMConfig().GetEnabled() == true) {
            const auto& tvmConfig = Config.GetTVMConfig();
            ctx.Register(CreateTVMSettingsUpdater(ctx.SelfID, tvmConfig.GetServiceTVMId(), tvmConfig.GetPublicKeys(), tvmConfig.GetUpdatePublicKeys(),
                                                        tvmConfig.GetUpdatePublicKeysSuccessTimeout(), tvmConfig.GetUpdatePublicKeysFailureTimeout()));
            RecreateTVMChecker(
                        Config.GetTVMConfig().GetServiceTVMId(),
                        Config.GetTVMConfig().GetPublicKeys(),
                        ctx);
            Y_VERIFY(TVMChecker);
        }

        if (Config.GetUseLoginProvider()) {
            UseLoginProvider = true;
        }
        if (AppData() && AppData()->DomainsInfo && !AppData()->DomainsInfo->Domains.empty()) {
            DomainName = "/" + AppData()->DomainsInfo->Domains.begin()->second->Name;
        }

        RefreshPeriod = TDuration::Parse(Config.GetRefreshPeriod());
        RefreshTime = TDuration::Parse(Config.GetRefreshTime());
        MinErrorRefreshTime = TDuration::Parse(Config.GetMinErrorRefreshTime());
        MaxErrorRefreshTime = TDuration::Parse(Config.GetMaxErrorRefreshTime());
        LifeTime = TDuration::Parse(Config.GetLifeTime());
        ExpireTime = TDuration::Parse(Config.GetExpireTime());
        TVMExpireTime = TDuration::Parse(Config.GetTVMExpireTime());

        NActors::TMon* mon = AppData(ctx)->Mon;
        if (mon) {
            NMonitoring::TIndexMonPage* actorsMonPage = mon->RegisterIndexPage("actors", "Actors");
            mon->RegisterActorPage(actorsMonPage, "ticket_parser", "Ticket Parser", false, ctx.ExecutorThread.ActorSystem, ctx.SelfID);
        }

        ctx.Schedule(RefreshPeriod, new NActors::TEvents::TEvWakeup());
        TBase::Become(&TThis::StateWork);
    }

    TTicketParser(const NKikimrProto::TAuthConfig& authConfig)
        : Config(authConfig) {}
};

IActor* CreateTicketParser(const NKikimrProto::TAuthConfig& authConfig) {
    InitOpenSSL();
    return new TTicketParser(authConfig);
}

}
