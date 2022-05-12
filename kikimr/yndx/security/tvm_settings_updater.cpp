#include <library/cpp/http/io/stream.h>
#include <library/cpp/openssl/io/stream.h>
#include <util/network/sock.h>
#include <library/cpp/tvmauth/version.h>
#include <library/cpp/actors/core/log.h>
#include <library/cpp/actors/core/actorsystem.h>
#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <ydb/core/base/appdata.h>
#include <ydb/core/base/counters.h>
#include <ydb/core/security/ticket_parser.h>
#include "tvm_settings_updater.h"


namespace NKikimr {

class TTVMSettingsUpdater : public TActorBootstrapped<TTVMSettingsUpdater> {
    using TThis = TTVMSettingsUpdater;
    using TBase = TActorBootstrapped<TTVMSettingsUpdater>;

    ui64 ServiceTVMId;
    TActorId TicketParser;
    TString InitialPublicKeys;
    bool UpdatePublicKeys;
    ui64 UpdatePublicKeysSuccessTimeout;
    ui64 UpdatePublicKeysFailureTimeout;
    NMonitoring::TDynamicCounters::TCounterPtr CounterUpdatesSuccess;
    NMonitoring::TDynamicCounters::TCounterPtr CounterUpdatesFails;
    NMonitoring::THistogramPtr CounterUpdateTime;

    void Handle(TEvTVMSettingsUpdater::TEvUpdateTVMPublicKeys::TPtr&, const TActorContext& ctx) {
        TStringStream error;
        try {
            TInstant start(TInstant::Now());
            TSocket s(TNetworkAddress("tvm-api.yandex.net", 443), TDuration::Seconds(15));
            TSocketOutput so(s);
            TSocketInput si(s);
            TOpenSslClientIO ssl(&si, &so);
            THttpOutput output(&ssl);
            output << "GET /2/keys?lib_version=" << NTvmAuth::LibVersion() << " HTTP/1.0\r\n"
                   << "Host: tvm-api.yandex.net\r\n\r\n";
            output.Finish();
            THttpInput input(&ssl);
            unsigned status = ParseHttpRetCode(input.FirstLine());
            CounterUpdateTime->Collect((TInstant::Now() - start).MilliSeconds());
            if (status == 200) {
                CounterUpdatesSuccess->Inc();
                TString response(input.ReadAll());
                LOG_DEBUG_S(ctx, NKikimrServices::TActivity::TVM_SETTINGS_UPDATER_ACTOR, "Got TVM2 public keys: \n" << response);
                ctx.Send(TicketParser, new TEvTVMSettingsUpdater::TEvUpdateTVMSettings(ServiceTVMId, response));
            } else {
                CounterUpdatesFails->Inc();
                error << "Error reading TVM2 public keys: " << status;
            }
        }
        catch (const std::exception& e) {
            error << "Exception while updating TVM public keys: " << e.what();
        }
        if (error.empty()) {
            ctx.Schedule(TDuration::Seconds(UpdatePublicKeysSuccessTimeout), new TEvTVMSettingsUpdater::TEvUpdateTVMPublicKeys());
        } else {
            LOG_ERROR_S(ctx, NKikimrServices::TActivity::TVM_SETTINGS_UPDATER_ACTOR, error.Str());
            ctx.Schedule(TDuration::Seconds(UpdatePublicKeysFailureTimeout), new TEvTVMSettingsUpdater::TEvUpdateTVMPublicKeys());
        }
    }

public:
    static constexpr NKikimrServices::TActivity::EType ActorActivityType() {
        return NKikimrServices::TActivity::TVM_SETTINGS_UPDATER_ACTOR;
    }

    TTVMSettingsUpdater(const TActorId& ticketParser, const ui64 serviceTVMId, const TString& initialPublicKeys, const bool updatePublicKeys, const ui64 updatePublicKeysSuccessTimeout, const ui64 updatePublicKeysFailureTimeout)
        : ServiceTVMId(serviceTVMId)
        , TicketParser(ticketParser)
        , InitialPublicKeys(initialPublicKeys)
        , UpdatePublicKeys(updatePublicKeys)
        , UpdatePublicKeysSuccessTimeout(updatePublicKeysSuccessTimeout)
        , UpdatePublicKeysFailureTimeout(updatePublicKeysFailureTimeout)
    {}

    void StateWork(TAutoPtr<NActors::IEventHandle>& ev, const NActors::TActorContext& ctx) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvTVMSettingsUpdater::TEvUpdateTVMPublicKeys, Handle);
        }
    }

    void Bootstrap(const TActorContext& ctx) {
        if (!InitialPublicKeys.empty()) {
            ctx.Send(TicketParser, new TEvTVMSettingsUpdater::TEvUpdateTVMSettings(ServiceTVMId, InitialPublicKeys));
        }
        if (UpdatePublicKeys) {
            ctx.Schedule(TDuration::Seconds(0), new TEvTVMSettingsUpdater::TEvUpdateTVMPublicKeys());
        }
        TIntrusivePtr<NMonitoring::TDynamicCounters> rootCounters = AppData(ctx)->Counters;
        TIntrusivePtr<NMonitoring::TDynamicCounters> authCounters = GetServiceCounters(rootCounters, "auth");
        NMonitoring::TDynamicCounterPtr counters = authCounters->GetSubgroup("subsystem", "TVM");
        CounterUpdatesSuccess = counters->GetCounter("PublicKeysUpdatesSuccess", true);
        CounterUpdatesFails = counters->GetCounter("PublicKeysUpdatesFails", true);
        CounterUpdateTime = counters->GetHistogram("PublicKeysUpdateTime",
                                                   NMonitoring::ExplicitHistogram({0, 1, 5, 10, 50, 100, 500, 1000, 2000, 5000, 10000, 30000}));
        TBase::Become(&TThis::StateWork);
    }
};

IActor* CreateTVMSettingsUpdater(const TActorId& ticketParser, const ui64 serviceTVMId, const TString& initialPublicKeys, const bool updatePublicKeys, const ui64 updatePublicKeysSuccessTimeout, const ui64 updatePublicKeysFailureTimeout) {
    return new TTVMSettingsUpdater(ticketParser, serviceTVMId, initialPublicKeys, updatePublicKeys, updatePublicKeysSuccessTimeout, updatePublicKeysFailureTimeout);
}

}
