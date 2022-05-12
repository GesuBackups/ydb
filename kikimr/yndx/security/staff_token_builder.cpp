#include <ydb/core/base/appdata.h>
#include <ydb/core/base/counters.h>
#include <library/cpp/actors/core/actorsystem.h>
#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/log.h>
#include <ydb/library/aclib/aclib.h>
#include <library/cpp/json/json_reader.h>
#include <library/cpp/json/json_value.h>
#include <library/cpp/http/io/stream.h>
#include <library/cpp/openssl/io/stream.h>
#include <util/network/sock.h>
#include "staff_token_builder.h"

namespace NKikimr {

class TStaffTokenBuilder : public NActors::TActorBootstrapped<TStaffTokenBuilder> {
    using TThis = TStaffTokenBuilder;
    using TBase = NActors::TActorBootstrapped<TStaffTokenBuilder>;

    TString Scheme = "https";
    TString Token;
    TString Host;
    TString Domain;
    NMonitoring::TDynamicCounters::TCounterPtr CounterBuildSuccess;
    NMonitoring::TDynamicCounters::TCounterPtr CounterBuildErrors;
    NMonitoring::THistogramPtr CounterBuildTime;

    const TString& GetHost() const {
        return Host;
    }

    TNetworkAddress GetNetworkAddress() const {
        TString host = GetHost();
        ui16 port = 0;
        TString::size_type colon = host.rfind(':');
        if (colon != TString::npos) {
            port = FromString(host.substr(colon + 1));
            host = host.substr(0, colon);
        } else {
            if (Scheme == "http") {
                port = 80;
            } else {
                port = 443;
            }
        }
        if (host.StartsWith('[') && host.EndsWith(']')) {
            host = host.substr(1, host.size() - 2);
        }
        return TNetworkAddress(host, port);
    }

    TDuration GetTimeout() const {
        return TDuration::Seconds(5);
    }

    const TString& GetToken() const {
        return Token;
    }

    void Handle(TEvStaffTokenBuilder::TEvBuildToken::TPtr& ev, const NActors::TActorContext& ctx) {
        ui64 uid(ev->Get()->UID);
        TString login(ev->Get()->Login);
        NACLib::TSID user = login.empty() ? ToString(uid) : login;
        TInstant start(TInstant::Now());
        TStringStream error;
        bool retryable = true;
        try {
            TSocket s(GetNetworkAddress(), GetTimeout());
            s.SetSocketTimeout(GetTimeout().MilliSeconds() / 1000, GetTimeout().MilliSeconds() % 1000);
            TSocketOutput so(s);
            TSocketInput si(s);
            IInputStream* inputStream = nullptr;
            IOutputStream* outputStream = nullptr;
            THolder<TOpenSslClientIO> ssl;
            if (Scheme == "https" ) {
                ssl = MakeHolder<TOpenSslClientIO>(&si, &so);
                inputStream = ssl.Get();
                outputStream = ssl.Get();
            } else if (Scheme == "http") {
                inputStream = &si;
                outputStream = &so;
            }
            THttpOutput output(outputStream);
            if (login) {
                output << "GET /v3/groupmembership?person.login=" << login << "&_fields=person.uid,group.url,group.ancestors.url&_limit=1000" " HTTP/1.0\r\n"
                       << "Host: " << GetHost() << "\r\n"
                       << "Authorization: OAuth " << GetToken() << "\r\n\r\n";
            } else {
                output << "GET /v3/groupmembership?person.uid=" << uid << "&_fields=person.login,group.url,group.ancestors.url&_limit=1000" " HTTP/1.0\r\n"
                       << "Host: " << GetHost() << "\r\n"
                       << "Authorization: OAuth " << GetToken() << "\r\n\r\n";
            }
            output.Finish();
            THttpInput input(inputStream);
            TVector<NACLib::TSID> groups;
            unsigned status = ParseHttpRetCode(input.FirstLine());
            if (status == 200) {
                TString response(input.ReadAll());
                LOG_DEBUG_S(ctx, NKikimrServices::TOKEN_BUILDER, "User " << user << " was responded to " << response);
                static NJson::TJsonReaderConfig readerConfig;
                NJson::TJsonValue jsonValue;
                if (NJson::ReadJsonTree(response, &readerConfig, &jsonValue, false)) {
                    NJson::TJsonValue jsonResult;
                    if (jsonValue.GetValue("result", &jsonResult)) {
                        if (jsonResult.IsArray()) {
                            for (const NJson::TJsonValue& result : jsonResult.GetArraySafe()) {
                                if (login.empty()) {
                                    const NJson::TJsonValue* jsonPerson;
                                    if (result.GetValuePointer("person", &jsonPerson)) {
                                        const NJson::TJsonValue* jsonLogin;
                                        if (jsonPerson->GetValuePointer("login", &jsonLogin)) {
                                            login = jsonLogin->GetStringRobust();
                                        }
                                        const NJson::TJsonValue* jsonUid;
                                        if (jsonPerson->GetValuePointer("uid", &jsonUid)) {
                                            uid = jsonUid->GetUIntegerRobust();
                                        }
                                    }
                                }
                                const NJson::TJsonValue* jsonGroup;
                                if (result.GetValuePointer("group", &jsonGroup)) {
                                    const NJson::TJsonValue* jsonUrl;
                                    if (jsonGroup->GetValuePointer("url", &jsonUrl)) {
                                        groups.emplace_back(jsonUrl->GetString() + "@" + Domain);
                                    }
                                    const NJson::TJsonValue* jsonAncestors;
                                    if (jsonGroup->GetValuePointer("ancestors", &jsonAncestors)) {
                                        for (const NJson::TJsonValue& jsonAncestor : jsonAncestors->GetArraySafe()) {
                                            if (jsonAncestor.GetValuePointer("url", &jsonUrl)) {
                                                groups.emplace_back(jsonUrl->GetString() + "@" + Domain);
                                            } else {
                                                error << "Wrong data in Staff response for user " << user << ": url not found";
                                            }
                                        }
                                    }
                                } else {
                                    error << "Wrong data in Staff response for user " << user << ": group not found";
                                }
                            }
                        } else {
                            error << "Wrong data in Staff response for user " << user << ": result is not an array";
                        }
                    } else {
                        error << "Wrong data in Staff response for user " << user << ": result not found";
                    }
                } else {
                    error << "Error parsing Staff response for user " << user << ": " << status;
                }
            } else {
                error << "Error reading Staff response for user " << user << ": " << status;
            }
            if (error.empty()) {
                CounterBuildSuccess->Inc();
                std::sort(groups.begin(), groups.end());
                groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
                ctx.Send(ev->Sender, new TEvStaffTokenBuilder::TEvBuildTokenResult(ev->Get()->Key, new NACLib::TUserToken({
                    .OriginalUserToken = ev->Get()->Key,
                    .UserSID = login + "@" + Domain,
                    .GroupSIDs = groups,
                    .AuthType = "Staff"
                })), 0, ev->Cookie);
            }
        }
        catch (const std::exception& e) {
            error << "Exception while requesting Staff for user " << user << ": " << e.what();
        }
        if (!error.empty()) {
            CounterBuildErrors->Inc();
            const TString& errorText(error.Str());
            LOG_ERROR_S(ctx, NKikimrServices::TOKEN_BUILDER, errorText);
            ctx.Send(ev->Sender, new TEvStaffTokenBuilder::TEvBuildTokenResult(ev->Get()->Key, {errorText, retryable}), 0, ev->Cookie);
        }
        CounterBuildTime->Collect((TInstant::Now() - start).MilliSeconds());
    }

public:
    static constexpr NKikimrServices::TActivity::EType ActorActivityType() { return NKikimrServices::TActivity::TOKEN_BUILDER_ACTOR; }

    TStaffTokenBuilder(const TString& token, const TString& host, const TString& domain)
        : Token(token)
        , Host(host)
        , Domain(domain)
    {
        TString::size_type pos = Host.find("://");
        if (pos != TString::npos) {
            Scheme = Host.substr(0, pos);
            Host = Host.substr(pos + 3);
            Y_VERIFY(Scheme == "https" || Scheme == "http");
        }
    }

    void Bootstrap(const TActorContext& ctx) {
        TIntrusivePtr<NMonitoring::TDynamicCounters> rootCounters = AppData(ctx)->Counters;
        TIntrusivePtr<NMonitoring::TDynamicCounters> authCounters = GetServiceCounters(rootCounters, "auth");
        NMonitoring::TDynamicCounterPtr counters = authCounters->GetSubgroup("subsystem", "Staff");
        CounterBuildSuccess = counters->GetCounter("TokenBuildSuccess", true);
        CounterBuildErrors = counters->GetCounter("TokenBuildErrors", true);
        CounterBuildTime = counters->GetHistogram("TokenBuildTimeMs",
                                                  NMonitoring::ExplicitHistogram({0, 1, 5, 10, 50, 100, 500, 1000, 2000, 5000}));
        TBase::Become(&TThis::StateWork);
    }

    void StateWork(TAutoPtr<NActors::IEventHandle>& ev, const NActors::TActorContext& ctx) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvStaffTokenBuilder::TEvBuildToken, Handle);
            CFunc(TEvents::TSystem::PoisonPill, Die);
        }
    }
};

IActor* CreateStaffTokenBuilder(const TString& token, const TString& host, const TString& domain) {
    return new TStaffTokenBuilder(token, host, domain);
}

}
