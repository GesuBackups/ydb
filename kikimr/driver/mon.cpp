#include "mon.h"
#include <ydb/core/mon/sync_http_mon.h>
#include <ydb/core/mon/async_http_mon.h>
#include <ydb/core/base/appdata.h>
#include <ydb/core/base/ticket_parser.h>
#include <kikimr/yndx/security/security.h>

namespace NKikimr {

static IEventHandle* RequestAuthorizer(const NActors::TActorId& owner, NMonitoring::IMonHttpRequest& request) {
    TStringBuf ydbSessionId = request.GetCookie("ydb_session_id"); // ydb internal session cookie
    TStringBuf sessionId = request.GetCookie("Session_id"); // yandex passport session cookie
    TStringBuf authorization = request.GetHeader("Authorization");
    if (!authorization.empty()) {
        return new NActors::IEventHandle(
            NKikimr::MakeTicketParserID(),
            owner,
            new NKikimr::TEvTicketParser::TEvAuthorizeTicket({
                .Ticket = TString(authorization)
            }),
            IEventHandle::FlagTrackDelivery
        );
    } else if (!ydbSessionId.empty()) {
        return new NActors::IEventHandle(
            NKikimr::MakeTicketParserID(),
            owner,
            new NKikimr::TEvTicketParser::TEvAuthorizeTicket({
                .Ticket = TString("Login ") + TString(ydbSessionId)
            }),
            IEventHandle::FlagTrackDelivery
        );
    } else if (!sessionId.empty()) {
        return new NActors::IEventHandle(
            NKikimr::MakeTicketParserID(),
            owner,
            new NKikimr::TEvTicketParser::TEvAuthorizeTicket({
                .Ticket = NKikimr::NSecurity::BlackBoxTokenFromSessionId(sessionId)
            }),
            IEventHandle::FlagTrackDelivery
        );
    } else if (NKikimr::AppData()->EnforceUserTokenRequirement && NKikimr::AppData()->DefaultUserSIDs.empty()) {
    return new NActors::IEventHandle(
        owner,
        owner,
        new NKikimr::TEvTicketParser::TEvAuthorizeTicketResult(TString(), {
            .Message = "No security credentials were provided",
            .Retryable = false
        })
    );
    } else if (!NKikimr::AppData()->DefaultUserSIDs.empty()) {
        TIntrusivePtr<NACLib::TUserToken> token = new NACLib::TUserToken(NKikimr::AppData()->DefaultUserSIDs);
        return new NActors::IEventHandle(
            owner,
            owner,
            new NKikimr::TEvTicketParser::TEvAuthorizeTicketResult(TString(), token, token->SerializeAsString())
        );
    } else {
        return nullptr;
    }
}

NActors::TMon* MonitoringFactory(NActors::TMon::TConfig config, const NKikimrConfig::TAppConfig& appConfig) {
    config.Authorizer = &RequestAuthorizer;
    if (appConfig.GetFeatureFlags().GetEnableAsyncHttpMon()) {
        return new NActors::TAsyncHttpMon(std::move(config));
    } else {
        return new NActors::TSyncHttpMon(std::move(config));
    }
}

}
