#include <library/cpp/actors/core/actorsystem.h>
#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/log.h>
#include <ydb/core/base/appdata.h>
#include <ydb/core/base/counters.h>
#include <ydb/library/aclib/aclib.h>
#include <ydb/library/security/util.h>
#include <library/cpp/json/json_reader.h>
#include <library/cpp/json/json_value.h>
#include <library/cpp/http/io/stream.h>
#include <library/cpp/openssl/io/stream.h>
#include <util/network/sock.h>
#include "blackbox_validator.h"
#include "ticket_parser.h"
#include "security.h"

namespace NKikimr {

class TBlackBoxValidator : public NActors::TActorBootstrapped<TBlackBoxValidator> {
    using TThis = TBlackBoxValidator;
    using TBase = NActors::TActorBootstrapped<TBlackBoxValidator>;

    TString Scheme = "https";
    TString Host;
    NMonitoring::TDynamicCounters::TCounterPtr CounterCheckSuccess;
    NMonitoring::TDynamicCounters::TCounterPtr CounterCheckErrors;
    NMonitoring::THistogramPtr CounterCheckTime;

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

    static constexpr TDuration GetTimeout() {
        return TDuration::Seconds(1);
    }

    static bool IsAllowedBlackBoxMethod(const TString& method) {
        return method == "oauth" || method == "sessionid";
    }

    void Handle(TEvBlackBoxValidator::TEvValidateTicket::TPtr& ev, const NActors::TActorContext& ctx) {
        TString key(ev->Get()->Key);
        TString ticket(ev->Get()->Ticket);
        TInstant start(TInstant::Now());
        TStringStream error;
        bool retryable = true;
        try {
            TNetworkAddress networkAddress = GetNetworkAddress();
            TSocket s(networkAddress, GetTimeout());
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
            TCgiParameters parameters;
            if (ticket.StartsWith("blackbox?")) {
                parameters.Scan(ticket.substr(9));
                if (!IsAllowedBlackBoxMethod(parameters.Get("method"))) {
                    throw yexception() << "Invalid blackbox method";
                }
            } else {
                parameters.InsertUnescaped("method", "oauth");
                parameters.InsertUnescaped("userip", NSecurity::DefaultUserIp());
                parameters.InsertUnescaped("oauth_token", ticket);
            }
            parameters.ReplaceUnescaped("format", "json");
            output << "GET /blackbox?" << parameters() << " HTTP/1.0\r\n";
            output << "Host: " << GetHost() << "\r\n\r\n";
            output.Finish();
            THttpInput input(inputStream);
            unsigned status = ParseHttpRetCode(input.FirstLine());

            if (status == 200) {
                TString response(input.ReadAll());
                LOG_DEBUG_S(ctx, NKikimrServices::BLACKBOX_VALIDATOR, "Ticket " << MaskTicket(ticket) << " was responded to " << response);
                static NJson::TJsonReaderConfig readerConfig;
                NJson::TJsonValue jsonValue;
                if (NJson::ReadJsonTree(response, &readerConfig, &jsonValue, false)) {
                    NJson::TJsonValue jsonStatus;
                    NJson::TJsonValue jsonStatusValue;
                    if (jsonValue.GetValue("status", &jsonStatus) && jsonStatus.GetValue("value", &jsonStatusValue)) {
                        const TString& status = jsonStatusValue.GetString();
                        if (status == "VALID") {
                            NJson::TJsonValue jsonUID;
                            NJson::TJsonValue jsonUIDValue;
                            if (jsonValue.GetValue("uid", &jsonUID) && jsonUID.GetValue("value", &jsonUIDValue)) {
                                ui64 uid = jsonUIDValue.GetUIntegerRobust();
                                LOG_INFO_S(ctx, NKikimrServices::BLACKBOX_VALIDATOR, "Ticket " << MaskTicket(ticket) << " was resolved to UID " << uid);
                                ctx.Send(ev->Sender, new TEvBlackBoxValidator::TEvValidateTicketResult(key, uid), 0, ev->Cookie);
                            } else {
                                error << "Error parsing BlackBox response for ticket " << MaskTicket(ticket) << ": 'uid' not found";
                            }
                        } else {
                            error << "Invalid BlackBox ticket " << MaskTicket(ticket) << ": " << status;
                            retryable = false;
                        }
                    } else {
                        error << "Error parsing BlackBox response for ticket " << MaskTicket(ticket) << ": 'status' not found";
                    }
                } else {
                    error << "Error parsing BlackBox response for ticket " << MaskTicket(ticket);
                }
            } else {
                error << "Error checking BlackBox ticket " << MaskTicket(ticket) << ": " << status;
            }
        }
        catch (const std::exception& e) {
            error << "Exception while checking ticket " << MaskTicket(ticket) << ": " << e.what();
        }

        if (!error.empty()) {
            CounterCheckErrors->Inc();
            const TString& errorText(error.Str());
            LOG_ERROR_S(ctx, NKikimrServices::BLACKBOX_VALIDATOR, errorText);
            ctx.Send(ev->Sender, new TEvBlackBoxValidator::TEvValidateTicketResult(key, {errorText, retryable}), 0, ev->Cookie);
        } else {
            CounterCheckSuccess->Inc();
        }
        CounterCheckTime->Collect((TInstant::Now() - start).MilliSeconds());
    }

public:
    static constexpr NKikimrServices::TActivity::EType ActorActivityType() { return NKikimrServices::TActivity::BLACKBOX_VALIDATOR_ACTOR; }

    TBlackBoxValidator(const TString& host)
        : Host(host)
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
        NMonitoring::TDynamicCounterPtr counters = authCounters->GetSubgroup("subsystem", "BlackBox");
        CounterCheckSuccess = counters->GetCounter("TokenCheckSuccess", true);
        CounterCheckErrors = counters->GetCounter("TokenCheckErrors", true);
        CounterCheckTime = counters->GetHistogram("TokenCheckTimeMs",
                                                  NMonitoring::ExplicitHistogram({0, 1, 5, 10, 50, 100, 500, 1000, 2000, 5000}));
        TBase::Become(&TThis::StateWork);
    }

    void StateWork(TAutoPtr<NActors::IEventHandle>& ev, const NActors::TActorContext& ctx) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvBlackBoxValidator::TEvValidateTicket, Handle);
            CFunc(TEvents::TSystem::PoisonPill, Die);
        }
    }
};

IActor* CreateBlackBoxValidator(const TString& host) {
    return new TBlackBoxValidator(host);
}

}
