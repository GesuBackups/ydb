#include <ydb/core/testlib/test_client.h>
#include <ydb/core/testlib/test_client.h>
#include <ydb/core/grpc_services/grpc_helper.h>
#include <library/cpp/grpc/server/grpc_server.h>
#include <ydb/public/lib/deprecated/kicli/kicli.h>
#include <ydb/public/sdk/cpp/client/ydb_driver/driver.h>
#include <ydb/public/sdk/cpp/client/ydb_table/table.h>
#include <library/cpp/tvmauth/unittest.h>
#include <library/cpp/testing/unittest/registar.h>
#include <library/cpp/testing/unittest/tests_data.h>
#include <kikimr/yndx/ycloud/api/access_service.h>
#include <kikimr/yndx/ycloud/api/user_account_service.h>
#include <kikimr/yndx/testlib/service_mocks/user_account_service_mock.h>
#include <library/cpp/actors/http/http_proxy.h>

#include "ticket_parser.h"
#include "tvm_settings_updater.h"

namespace NKikimr {

class TAccessServiceMock : public yandex::cloud::priv::servicecontrol::v1::AccessService::Service {
public:
    std::atomic_uint64_t AuthenticateCount = 0;
    std::atomic_uint64_t AuthorizeCount= 0;

    THashSet<TString> InvalidTokens = {"invalid"};
    THashSet<TString> UnavailableTokens;
    THashSet<TString> AllowedUserTokens = {"user1"};
    THashMap<TString, TString> AllowedServiceTokens = {{"service1", "root1/folder1"}};

    virtual grpc::Status Authenticate(
            grpc::ServerContext*,
            const yandex::cloud::priv::servicecontrol::v1::AuthenticateRequest* request,
            yandex::cloud::priv::servicecontrol::v1::AuthenticateResponse* response) override {

        ++AuthenticateCount;
        TString token = request->iam_token();
        if (InvalidTokens.count(token) > 0) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid Token");
        }
        if (UnavailableTokens.count(token) > 0) {
            return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Service Unavailable");
        }
        if (AllowedUserTokens.count(token) > 0) {
            response->mutable_subject()->mutable_user_account()->set_id(token);
            return grpc::Status::OK;
        }
        if (AllowedServiceTokens.count(token) > 0) {
            response->mutable_subject()->mutable_service_account()->set_id(token);
            response->mutable_subject()->mutable_service_account()->set_folder_id(AllowedServiceTokens[token]);
            return grpc::Status::OK;
        }
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "Access Denied");
    }

    THashSet<TString> AllowedUserPermissions = {"user1-something.read"};
    THashMap<TString, TString> AllowedServicePermissions = {{"service1-something.write", "root1/folder1"}};
    THashSet<TString> AllowedResourceIds = {};
    THashSet<TString> UnavailableUserPermissions;

    virtual grpc::Status Authorize(
            grpc::ServerContext*,
            const yandex::cloud::priv::servicecontrol::v1::AuthorizeRequest* request,
            yandex::cloud::priv::servicecontrol::v1::AuthorizeResponse* response) override {
        ++AuthorizeCount;
        TString token = request->iam_token();
        if (UnavailableUserPermissions.count(token + '-' + request->permission()) > 0) {
            return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Service Unavailable");
        }
        bool allowedResource = true;
        if (!AllowedResourceIds.empty()) {
            allowedResource = false;
            for (const auto& resourcePath : request->resource_path()) {
                if (AllowedResourceIds.count(resourcePath.id()) > 0) {
                    allowedResource = true;
                }
            }
        }
        if (allowedResource) {
            if (AllowedUserPermissions.count(token + '-' + request->permission()) > 0) {
                response->mutable_subject()->mutable_user_account()->set_id(token);
                return grpc::Status::OK;
            }
            if (AllowedServicePermissions.count(token + '-' + request->permission()) > 0) {
                response->mutable_subject()->mutable_service_account()->set_id(token);
                response->mutable_subject()->mutable_service_account()->set_folder_id(AllowedServicePermissions[token + '-' + request->permission()]);
                return grpc::Status::OK;
            }
        }
        return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "Access Denied");
    }
};

Y_UNIT_TEST_SUITE(TTicketParserYandexTest) {

    Y_UNIT_TEST(Basic) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);

        NKikimrProto::TAuthConfig authConfig;
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();

        TTestActorRuntime* runtime = server.GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        auto ticket1 = "3:serv:CBAQ__________9_IgQIARAK:KY-4ZauFQ22JHeozffUSBkhasAV031No0ZT8jky86FdPRG52bL-knutOya3NQzVasM-6k67-D9pGp7xtybWmQ8Q5NWG5O-W4USsX9HqETqNqUL_uRIYtfuezGQ5NUpSp1jJeEyIPqu1mJU9v8gNtZEz7aq_A0n15pnvxRdF0U60"; // for src=1, dst=10
        auto ticket2 = "3:serv:CBAQ__________9_IgQIAhAK:WZiSyM6vYyAuE70ya4WFJrBz0-JGH4erfSc4wf8V0QhkNVyM5yE3skU4NFKOXjheG22yzI6e-PdaoHbYGOzrrTfuMqHpNjv4lGf9OSeVK1B-X-4lL_P9Zo1VCFOcOSo4LKaAG1c3fm4-eeL0jDGy-w_LedqhWGHlyvUKxbzNyU0"; // for src=2, dst=10
        auto publicKeys = NTvmAuth::NUnittest::TVMKNIFE_PUBLIC_KEYS;


        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTVMSettingsUpdater::TEvUpdateTVMSettings(11, publicKeys), 0));
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(ticket2)), 0);
        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT_STRING_CONTAINS(result->Error.Message, "Invalid ticket destination");

        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTVMSettingsUpdater::TEvUpdateTVMSettings(10, publicKeys), 0));
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(ticket1)), 0);
        THolder<NACLib::TUserToken> token = THolder<NACLib::TUserToken>(new NACLib::TUserToken({
            .OriginalUserToken = ticket1,
            .UserSID = "1@tvm",
            .AuthType = "TVM"
        }));
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT_EQUAL(result->Token->SerializeAsString(), token->SerializeAsString());
    }

    Y_UNIT_TEST(TwiceAuthenticationOkOk) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 accessServicePort = tp.GetPort(4284);
        ui16 blackBoxPort = tp.GetPort(2280);
        TString accessServiceEndpoint = "localhost:" + ToString(accessServicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(true);
        authConfig.SetBlackBoxEndpoint("http://[::]:" + ToString(blackBoxPort));
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        TString userToken = "user1";

        // Black Box 'Mock'
        TActorId httpProxyId = runtime->Register(NHttp::CreateHttpProxy());
        TActorId serverId = runtime->AllocateEdgeActor();
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvAddListeningPort(blackBoxPort)), 0, true);
        TAutoPtr<IEventHandle> handle;
        runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvConfirmListen>(handle);

        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/blackbox", serverId)), 0, true);

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());

        runtime->Send(new IEventHandle(MakeTicketParserID(), runtime->AllocateEdgeActor(), new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);

        // black box handling
        NHttp::TEvHttpProxy::TEvHttpIncomingRequest* request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/blackbox?"));
        NHttp::THttpOutgoingResponsePtr httpResponse = request->Request->CreateResponseOK(R"___(
        {"oauth":{"uid":"1110000000011111","token_id":"111111","device_id":"","device_name":"","scope":"startrek:write yt:api stat:all yql:api ydb:api","ctime":"2016-08-24 19:34:05","issue_time":"2018-12-05 19:13:17","expire_time":null,"is_ttl_refreshable":false,"client_id":"111111111111111111111111111111111","client_name":"YQL","client_icon":"https:\/\/avatars.mds.yandex.net\/get-oauth\/11111\/11111111111111111111111111111111-11111111111111111111111111111111\/normal","client_homepage":"https:\/\/yql.yandex-team.ru\/","client_ctime":"2016-02-02 13:46:09","client_is_yandex":false,"xtoken_id":"","meta":""},"uid":{"value":"1110000000011111","lite":false,"hosted":false},"login":"user1","have_password":true,"have_hint":false,"karma":{"value":0},"karma_status":{"value":0},"status":{"value":"VALID","id":0},"error":"OK","connection_id":"t:111111"}
                                                                                          )___", "application/json");
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);
        // black box handling

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
    }

    /*Y_UNIT_TEST(TwiceAuthenticationFailOk) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 accessServicePort = tp.GetPort(4284);
        ui16 blackBoxPort = tp.GetPort(2280);
        TString accessServiceEndpoint = "localhost:" + ToString(accessServicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(true);
        authConfig.SetBlackBoxEndpoint("http://[::]:" + ToString(blackBoxPort));
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        TString userToken = "user1";

        // Black Box 'Mock'
        TActorId httpProxyId = runtime->Register(NHttp::CreateHttpProxy());
        TActorId serverId = runtime->AllocateEdgeActor();
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvAddListeningPort(blackBoxPort)), 0, true);
        TAutoPtr<IEventHandle> handle;
        runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvConfirmListen>(handle);

        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/blackbox", serverId)), 0, true);

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());

        runtime->Send(new IEventHandle(MakeTicketParserID(), runtime->AllocateEdgeActor(), new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);

        // black box handling
        NHttp::TEvHttpProxy::TEvHttpIncomingRequest* request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/blackbox?"));
        NHttp::THttpOutgoingResponsePtr httpResponse = request->Request->CreateResponseGatewayTimeout();
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);
        // black box handling

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
    }*/

    Y_UNIT_TEST(TwiceAuthenticationOkFail) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 accessServicePort = tp.GetPort(4284);
        ui16 blackBoxPort = tp.GetPort(2280);
        TString accessServiceEndpoint = "localhost:" + ToString(accessServicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(true);
        authConfig.SetBlackBoxEndpoint("http://[::]:" + ToString(blackBoxPort));
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        TString userToken = "user1";

        // Black Box 'Mock'
        TActorId httpProxyId = runtime->Register(NHttp::CreateHttpProxy());
        TActorId serverId = runtime->AllocateEdgeActor();
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvAddListeningPort(blackBoxPort)), 0, true);
        TAutoPtr<IEventHandle> handle;
        runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvConfirmListen>(handle);

        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/blackbox", serverId)), 0, true);

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());
        accessServiceMock.AllowedUserTokens.clear();

        runtime->Send(new IEventHandle(MakeTicketParserID(), runtime->AllocateEdgeActor(), new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);

        // black box handling
        NHttp::TEvHttpProxy::TEvHttpIncomingRequest* request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/blackbox?"));
        NHttp::THttpOutgoingResponsePtr httpResponse = request->Request->CreateResponseGatewayTimeout();
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);
        // black box handling

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(!result->Error.empty());
    }

    Y_UNIT_TEST(TwiceAuthenticationFailFail) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 accessServicePort = tp.GetPort(4284);
        ui16 blackBoxPort = tp.GetPort(2280);
        TString accessServiceEndpoint = "localhost:" + ToString(accessServicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(true);
        authConfig.SetBlackBoxEndpoint("http://[::]:" + ToString(blackBoxPort));
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        TString userToken = "user1";

        // Black Box 'Mock'
        TActorId httpProxyId = runtime->Register(NHttp::CreateHttpProxy());
        TActorId serverId = runtime->AllocateEdgeActor();
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvAddListeningPort(blackBoxPort)), 0, true);
        TAutoPtr<IEventHandle> handle;
        runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvConfirmListen>(handle);

        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/blackbox", serverId)), 0, true);

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());
        accessServiceMock.AllowedUserTokens.clear();

        TActorId sender = runtime->AllocateEdgeActor();

        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);

        // black box handling
        NHttp::TEvHttpProxy::TEvHttpIncomingRequest* request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/blackbox?"));
        NHttp::THttpOutgoingResponsePtr httpResponse = request->Request->CreateResponseOK(R"___(
        {"oauth":{"uid":"1110000000011111","token_id":"111111","device_id":"","device_name":"","scope":"startrek:write yt:api stat:all yql:api ydb:api","ctime":"2016-08-24 19:34:05","issue_time":"2018-12-05 19:13:17","expire_time":null,"is_ttl_refreshable":false,"client_id":"111111111111111111111111111111111","client_name":"YQL","client_icon":"https:\/\/avatars.mds.yandex.net\/get-oauth\/11111\/11111111111111111111111111111111-11111111111111111111111111111111\/normal","client_homepage":"https:\/\/yql.yandex-team.ru\/","client_ctime":"2016-02-02 13:46:09","client_is_yandex":false,"xtoken_id":"","meta":""},"uid":{"value":"1110000000011111","lite":false,"hosted":false},"login":"user1","have_password":true,"have_hint":false,"karma":{"value":0},"karma_status":{"value":0},"status":{"value":"VALID","id":0},"error":"OK","connection_id":"t:111111"}
                                                                                          )___", "application/json");
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);
        // black box handling

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
    }

    Y_UNIT_TEST(TwiceAuthenticationWithType1) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 accessServicePort = tp.GetPort(4284);
        ui16 blackBoxPort = tp.GetPort(2280);
        TString accessServiceEndpoint = "localhost:" + ToString(accessServicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(true);
        authConfig.SetBlackBoxEndpoint("http://[::]:" + ToString(blackBoxPort));
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        TString userToken = "user1";

        // Black Box 'Mock'
        TActorId httpProxyId = runtime->Register(NHttp::CreateHttpProxy());
        TActorId serverId = runtime->AllocateEdgeActor();
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvAddListeningPort(blackBoxPort)), 0, true);
        TAutoPtr<IEventHandle> handle;
        runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvConfirmListen>(handle);

        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/blackbox", serverId)), 0, true);

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());

        runtime->Send(new IEventHandle(MakeTicketParserID(), runtime->AllocateEdgeActor(), new TEvTicketParser::TEvAuthorizeTicket("Bearer " + userToken)), 0);

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
    }

    Y_UNIT_TEST(TwiceAuthenticationWithType2) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 accessServicePort = tp.GetPort(4284);
        ui16 blackBoxPort = tp.GetPort(2280);
        TString accessServiceEndpoint = "localhost:" + ToString(accessServicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(true);
        authConfig.SetBlackBoxEndpoint("http://[::]:" + ToString(blackBoxPort));
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        TString userToken = "user1";

        // Black Box 'Mock'
        TActorId httpProxyId = runtime->Register(NHttp::CreateHttpProxy());
        TActorId serverId = runtime->AllocateEdgeActor();
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvAddListeningPort(blackBoxPort)), 0, true);
        TAutoPtr<IEventHandle> handle;
        runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvConfirmListen>(handle);

        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/blackbox", serverId)), 0, true);

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());

        runtime->Send(new IEventHandle(MakeTicketParserID(), runtime->AllocateEdgeActor(), new TEvTicketParser::TEvAuthorizeTicket("OAuth " + userToken)), 0);

        // black box handling
        NHttp::TEvHttpProxy::TEvHttpIncomingRequest* request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/blackbox?"));
        NHttp::THttpOutgoingResponsePtr httpResponse = request->Request->CreateResponseOK(R"___(
        {"oauth":{"uid":"1110000000011111","token_id":"111111","device_id":"","device_name":"","scope":"startrek:write yt:api stat:all yql:api ydb:api","ctime":"2016-08-24 19:34:05","issue_time":"2018-12-05 19:13:17","expire_time":null,"is_ttl_refreshable":false,"client_id":"111111111111111111111111111111111","client_name":"YQL","client_icon":"https:\/\/avatars.mds.yandex.net\/get-oauth\/11111\/11111111111111111111111111111111-11111111111111111111111111111111\/normal","client_homepage":"https:\/\/yql.yandex-team.ru\/","client_ctime":"2016-02-02 13:46:09","client_is_yandex":false,"xtoken_id":"","meta":""},"uid":{"value":"1110000000011111","lite":false,"hosted":false},"login":"user1","have_password":true,"have_hint":false,"karma":{"value":0},"karma_status":{"value":0},"status":{"value":"VALID","id":0},"error":"OK","connection_id":"t:111111"}
                                                                                          )___", "application/json");
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);
        // black box handling

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
    }

    Y_UNIT_TEST(AuthenticationWithUserAccount) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        TString accessServiceEndpoint = "localhost:" + ToString(tp.GetPort(4284));
        TString userAccountServiceEndpoint = "localhost:" + ToString(tp.GetPort(4285));
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseStaff(false);
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseUserAccountService(true);
        authConfig.SetUseUserAccountServiceTLS(false);
        authConfig.SetUserAccountServiceEndpoint(userAccountServiceEndpoint);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();

        TString userToken = "user1";

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder1;
        builder1.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder1.BuildAndStart());

        // User Account Service Mock
        TUserAccountServiceMock userAccountServiceMock;
        auto& user1 = userAccountServiceMock.UserAccountData["user1"];
        user1.mutable_yandex_passport_user_account()->set_login("login1");
        grpc::ServerBuilder builder2;
        builder2.AddListeningPort(userAccountServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&userAccountServiceMock);
        std::unique_ptr<grpc::Server> userAccountServer(builder2.BuildAndStart());

        TTestActorRuntime* runtime = server.GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);
        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT_VALUES_EQUAL(result->Token->GetUserSID(), "login1@passport");
    }

    Y_UNIT_TEST(AuthenticationUnavailable) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(4284);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();

        TString userToken = "user1";

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());

        TTestActorRuntime* runtime = server.GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        accessServiceMock.UnavailableTokens.insert(userToken);
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);
        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(!result->Error.empty());
        UNIT_ASSERT(result->Error.Retryable);
    }

    Y_UNIT_TEST(AuthenticationUnsupported) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(4284);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();

        TString userToken = "OAuth user1";

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());

        TTestActorRuntime* runtime = server.GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        accessServiceMock.UnavailableTokens.insert(userToken);
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);
        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.Message == "Token is not supported");
        UNIT_ASSERT(!result->Error.Retryable);
    }

    Y_UNIT_TEST(AuthenticationUnknown) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(4284);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();

        TString userToken = "bebebe user1";

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());

        TTestActorRuntime* runtime = server.GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        accessServiceMock.UnavailableTokens.insert(userToken);
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);
        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.Message == "Unknown token");
        UNIT_ASSERT(!result->Error.Retryable);
    }

    Y_UNIT_TEST(Authorization) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(4284);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();

        TString userToken = "user1";

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());

        TTestActorRuntime* runtime = server.GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        // Authorization successful.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           {"something.read"})), 0);
        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token->IsExist("something.read-bbbb4554@as"));
        UNIT_ASSERT(!result->Token->IsExist("something.write-bbbb4554@as"));

        // Authorization failure with not enough permissions.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           {"something.write"})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(!result->Error.empty());
        UNIT_ASSERT_VALUES_EQUAL(result->Error.Message, "Access Denied");
        UNIT_ASSERT(!result->Error.Retryable);

        // Authorization successful.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           {"something.read"})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token->IsExist("something.read-bbbb4554@as"));
        UNIT_ASSERT(!result->Token->IsExist("something.write-bbbb4554@as"));

        // Authorization failure with invalid token.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           "invalid",
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           {"something.read"})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT_VALUES_EQUAL(result->Error.Message, "Access Denied");
        UNIT_ASSERT(!result->Error.Retryable);

        // Authorization failure with access denied token.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           "invalid-token1",
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           {"something.read"})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT_VALUES_EQUAL(result->Error.Message, "Access Denied");
        UNIT_ASSERT(!result->Error.Retryable);

        // Authorization failure with wrong folder_id.
        accessServiceMock.AllowedResourceIds.emplace("cccc1234");
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "XXXXXXXX"}, {"database_id", "XXXXXXXX"}},
                                           {"something.read"})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error);
        UNIT_ASSERT_VALUES_EQUAL(result->Error.Message, "Access Denied");
        UNIT_ASSERT(!result->Error.Retryable);

        // Authorization successful with right folder_id.
        accessServiceMock.AllowedResourceIds.emplace("aaaa1234");
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "XXXXXXXX"}},
                                           {"something.read"})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token->IsExist("something.read-XXXXXXXX@as"));

        // Authorization successful with right database_id.
        accessServiceMock.AllowedResourceIds.clear();
        accessServiceMock.AllowedResourceIds.emplace("bbbb4554");
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "XXXXXXXX"}, {"database_id", "bbbb4554"}},
                                           {"something.read"})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token->IsExist("something.read-bbbb4554@as"));
    }

    Y_UNIT_TEST(AuthorizationWithRequiredPermissions) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(4284);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();

        TString userToken = "user1";

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());

        TTestActorRuntime* runtime = server.GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        // Authorization successful.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           TVector<TEvTicketParser::TEvAuthorizeTicket::TPermission>{TEvTicketParser::TEvAuthorizeTicket::Optional("something.read"), TEvTicketParser::TEvAuthorizeTicket::Optional("something.write")})), 0);
        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token->IsExist("something.read-bbbb4554@as"));
        UNIT_ASSERT(!result->Token->IsExist("something.write-bbbb4554@as"));

        // Authorization failure with not enough permissions.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           TVector<TEvTicketParser::TEvAuthorizeTicket::TPermission>{TEvTicketParser::TEvAuthorizeTicket::Optional("something.read"), TEvTicketParser::TEvAuthorizeTicket::Required("something.write")})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(!result->Error.empty());
        UNIT_ASSERT_VALUES_EQUAL(result->Error.Message, "something.write for folder_id aaaa1234 - Access Denied");
        UNIT_ASSERT(!result->Error.Retryable);
    }

    Y_UNIT_TEST(AuthorizationWithUserAccount) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        TString accessServiceEndpoint = "localhost:" + ToString(tp.GetPort(4284));
        TString userAccountServiceEndpoint = "localhost:" + ToString(tp.GetPort(4285));
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseStaff(false);
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseUserAccountService(true);
        authConfig.SetUseUserAccountServiceTLS(false);
        authConfig.SetUserAccountServiceEndpoint(userAccountServiceEndpoint);
        // placemark1
        authConfig.SetCacheAccessServiceAuthorization(false);
        //
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();

        TString userToken = "user1";

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder1;
        builder1.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder1.BuildAndStart());

        // User Account Service Mock
        TUserAccountServiceMock userAccountServiceMock;
        auto& user1 = userAccountServiceMock.UserAccountData["user1"];
        user1.mutable_yandex_passport_user_account()->set_login("login1");
        grpc::ServerBuilder builder2;
        builder2.AddListeningPort(userAccountServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&userAccountServiceMock);
        std::unique_ptr<grpc::Server> userAccountServer(builder2.BuildAndStart());

        TTestActorRuntime* runtime = server.GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        // Authorization successful.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           {"something.read"})), 0);
        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token->IsExist("something.read-bbbb4554@as"));
        UNIT_ASSERT(!result->Token->IsExist("something.write-bbbb4554@as"));
        UNIT_ASSERT_VALUES_EQUAL(result->Token->GetUserSID(), "login1@passport");

        // Authorization failure with not enough permissions.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           {"something.write"})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(!result->Error.empty());
        UNIT_ASSERT_VALUES_EQUAL(result->Error.Message, "Access Denied");
        UNIT_ASSERT(!result->Error.Retryable);

        // Authorization successful.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           {"something.read"})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token->IsExist("something.read-bbbb4554@as"));
        UNIT_ASSERT(!result->Token->IsExist("something.write-bbbb4554@as"));
        UNIT_ASSERT_VALUES_EQUAL(result->Token->GetUserSID(), "login1@passport");

        accessServiceMock.AllowedUserPermissions.insert("user1-something.write");

        // Authorization successful - 2
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           TVector<TString>{"something.read", "something.write"})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token->IsExist("something.read-bbbb4554@as"));
        // placemark 1
        UNIT_ASSERT(result->Token->IsExist("something.write-bbbb4554@as"));
        UNIT_ASSERT_VALUES_EQUAL(result->Token->GetUserSID(), "login1@passport");
    }

    Y_UNIT_TEST(AuthorizationWithUserAccount2) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        TString accessServiceEndpoint = "localhost:" + ToString(tp.GetPort(4284));
        TString userAccountServiceEndpoint = "localhost:" + ToString(tp.GetPort(4285));
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseStaff(false);
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseUserAccountService(true);
        authConfig.SetUseUserAccountServiceTLS(false);
        authConfig.SetUserAccountServiceEndpoint(userAccountServiceEndpoint);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();

        TString userToken = "user1";

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder1;
        builder1.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder1.BuildAndStart());

        // User Account Service Mock
        TUserAccountServiceMock userAccountServiceMock;
        auto& user1 = userAccountServiceMock.UserAccountData["user1"];
        user1.mutable_yandex_passport_user_account()->set_login("login1");
        grpc::ServerBuilder builder2;
        builder2.AddListeningPort(userAccountServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&userAccountServiceMock);
        std::unique_ptr<grpc::Server> userAccountServer(builder2.BuildAndStart());

        TTestActorRuntime* runtime = server.GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        accessServiceMock.AllowedUserPermissions.insert("user1-something.write");
        accessServiceMock.AllowedUserPermissions.erase("user1-something.list");
        accessServiceMock.AllowedUserPermissions.erase("user1-something.read");

        // Authorization successful - 2
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           {"something.list", "something.read", "something.write", "something.eat", "somewhere.sleep"})), 0);
        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(!result->Token->IsExist("something.read-bbbb4554@as"));
        UNIT_ASSERT(!result->Token->IsExist("something.list-bbbb4554@as"));
        UNIT_ASSERT(result->Token->IsExist("something.write-bbbb4554@as"));
        UNIT_ASSERT_VALUES_EQUAL(result->Token->GetUserSID(), "login1@passport");
    }

    Y_UNIT_TEST(AuthorizationUnavailable) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(4284);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();

        TString userToken = "user1";

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());

        TTestActorRuntime* runtime = server.GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        accessServiceMock.UnavailableUserPermissions.insert(userToken + "-something.write");

        // Authorization unsuccessfull.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           TVector<TString>{"something.read", "something.write"})), 0);
        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(!result->Error.empty());
        UNIT_ASSERT(result->Error.Retryable);
    }

    Y_UNIT_TEST(AuthorizationModify) {
        using namespace Tests;

        TPortManager tp;
        ui16 port = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(4284);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseAccessService(true);
        authConfig.SetUseAccessServiceTLS(false);
        authConfig.SetAccessServiceEndpoint(accessServiceEndpoint);
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(port, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();

        TString userToken = "user1";

        // Access Server Mock
        NKikimr::TAccessServiceMock accessServiceMock;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(accessServiceEndpoint, grpc::InsecureServerCredentials()).RegisterService(&accessServiceMock);
        std::unique_ptr<grpc::Server> accessServer(builder.BuildAndStart());

        TTestActorRuntime* runtime = server.GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TAutoPtr<IEventHandle> handle;

        // Authorization successful.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           {"something.read"})), 0);
        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token->IsExist("something.read-bbbb4554@as"));
        UNIT_ASSERT(!result->Token->IsExist("something.write-bbbb4554@as"));

        accessServiceMock.AllowedUserPermissions.insert(userToken + "-something.write");
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvDiscardTicket(userToken)), 0);

        // Authorization successful with new permissions.
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(
                                           userToken,
                                           {{"folder_id", "aaaa1234"}, {"database_id", "bbbb4554"}},
                                           TVector<TString>{"something.read", "something.write"})), 0);
        result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token->IsExist("something.read-bbbb4554@as"));
        UNIT_ASSERT(result->Token->IsExist("something.write-bbbb4554@as"));
    }

    Y_UNIT_TEST(BlackBoxGood) {
        using namespace Tests;
        TPortManager tp;
        ui16 kikimrPort = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(2280);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(true);
        authConfig.SetBlackBoxEndpoint("http://[::]:" + ToString(servicePort));
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(kikimrPort, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        TString userToken = "user1";

        TActorId httpProxyId = runtime->Register(NHttp::CreateHttpProxy());
        TActorId serverId = runtime->AllocateEdgeActor();
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvAddListeningPort(servicePort)), 0, true);
        TAutoPtr<IEventHandle> handle;
        runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvConfirmListen>(handle);

        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/blackbox", serverId)), 0, true);

        TActorId sender = runtime->AllocateEdgeActor();
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);

        NHttp::TEvHttpProxy::TEvHttpIncomingRequest* request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/blackbox?"));
        NHttp::THttpOutgoingResponsePtr httpResponse = request->Request->CreateResponseOK(R"___(
        {"oauth":{"uid":"1110000000011111","token_id":"111111","device_id":"","device_name":"","scope":"startrek:write yt:api stat:all yql:api ydb:api","ctime":"2016-08-24 19:34:05","issue_time":"2018-12-05 19:13:17","expire_time":null,"is_ttl_refreshable":false,"client_id":"111111111111111111111111111111111","client_name":"YQL","client_icon":"https:\/\/avatars.mds.yandex.net\/get-oauth\/11111\/11111111111111111111111111111111-11111111111111111111111111111111\/normal","client_homepage":"https:\/\/yql.yandex-team.ru\/","client_ctime":"2016-02-02 13:46:09","client_is_yandex":false,"xtoken_id":"","meta":""},"uid":{"value":"1110000000011111","lite":false,"hosted":false},"login":"user1","have_password":true,"have_hint":false,"karma":{"value":0},"karma_status":{"value":0},"status":{"value":"VALID","id":0},"error":"OK","connection_id":"t:111111"}
                                                                                          )___", "application/json");
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token != nullptr);
        UNIT_ASSERT_EQUAL(result->Token->GetUserSID(), "1110000000011111@blackbox");
    }

    Y_UNIT_TEST(LoginGood) {
        using namespace Tests;
        TPortManager tp;
        ui16 kikimrPort = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseLoginProvider(true);
        auto settings = TServerSettings(kikimrPort, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        NLogin::TLoginProvider provider;

        provider.Audience = "/Root";
        provider.RotateKeys();

        TActorId sender = runtime->AllocateEdgeActor();
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvUpdateLoginSecurityState(provider.GetSecurityState())), 0);

        provider.CreateUser({.User = "user1", .Password = "password1"});
        auto loginResponse = provider.LoginUser({.User = "user1", .Password = "password1"});

        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(loginResponse.Token)), 0);

        TAutoPtr<IEventHandle> handle;

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token != nullptr);
        UNIT_ASSERT_EQUAL(result->Token->GetUserSID(), "user1");
    }

    Y_UNIT_TEST(LoginGoodWithGroups) {
        using namespace Tests;
        TPortManager tp;
        ui16 kikimrPort = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseLoginProvider(true);
        auto settings = TServerSettings(kikimrPort, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        NLogin::TLoginProvider provider;

        provider.Audience = "/Root";
        provider.RotateKeys();

        TActorId sender = runtime->AllocateEdgeActor();

        provider.CreateGroup({.Group = "group1"});
        provider.CreateUser({.User = "user1", .Password = "password1"});
        provider.AddGroupMembership({.Group = "group1", .Member = "user1"});

        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvUpdateLoginSecurityState(provider.GetSecurityState())), 0);

        auto loginResponse = provider.LoginUser({.User = "user1", .Password = "password1"});

        UNIT_ASSERT_EQUAL(loginResponse.Error, "");

        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(loginResponse.Token)), 0);

        TAutoPtr<IEventHandle> handle;

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token != nullptr);
        UNIT_ASSERT_EQUAL(result->Token->GetUserSID(), "user1");
        UNIT_ASSERT(result->Token->IsExist("group1"));
    }

    Y_UNIT_TEST(LoginBad) {
        using namespace Tests;
        TPortManager tp;
        ui16 kikimrPort = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(false);
        authConfig.SetUseLoginProvider(true);
        auto settings = TServerSettings(kikimrPort, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        NLogin::TLoginProvider provider;

        provider.Audience = "/Root";
        provider.RotateKeys();

        TActorId sender = runtime->AllocateEdgeActor();
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvUpdateLoginSecurityState(provider.GetSecurityState())), 0);

        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket("Login bad-token")), 0);

        TAutoPtr<IEventHandle> handle;

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(!result->Error.empty());
        UNIT_ASSERT_VALUES_EQUAL(result->Error.Message, "Token is not in correct format");
    }

    Y_UNIT_TEST(BlackBoxBad) {
        using namespace Tests;
        TPortManager tp;
        ui16 kikimrPort = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(2280);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(true);
        authConfig.SetBlackBoxEndpoint("http://[::]:" + ToString(servicePort));
        authConfig.SetUseStaff(false);
        auto settings = TServerSettings(kikimrPort, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        TString userToken = "user1";

        TActorId httpProxyId = runtime->Register(NHttp::CreateHttpProxy());
        TActorId serverId = runtime->AllocateEdgeActor();
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvAddListeningPort(servicePort)), 0, true);
        TAutoPtr<IEventHandle> handle;
        runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvConfirmListen>(handle);

        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/blackbox", serverId)), 0, true);

        TActorId sender = runtime->AllocateEdgeActor();
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);

        NHttp::TEvHttpProxy::TEvHttpIncomingRequest* request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/blackbox?"));
        NHttp::THttpOutgoingResponsePtr httpResponse = request->Request->CreateResponseGatewayTimeout();
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT_VALUES_EQUAL(result->Error.Message, "Error checking BlackBox ticket **** (8E120919): 504");
        UNIT_ASSERT(result->Error.Retryable);
        UNIT_ASSERT(result->Token == nullptr);
    }

    Y_UNIT_TEST(BlackBoxAndStaffGood) {
        using namespace Tests;
        TPortManager tp;
        ui16 kikimrPort = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(2280);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(true);
        authConfig.SetBlackBoxEndpoint("http://[::]:" + ToString(servicePort));
        authConfig.SetUseStaff(true);
        authConfig.SetStaffEndpoint("http://[::]:" + ToString(servicePort));
        auto settings = TServerSettings(kikimrPort, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        TString userToken = "user1";

        TActorId httpProxyId = runtime->Register(NHttp::CreateHttpProxy());
        TActorId serverId = runtime->AllocateEdgeActor();
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvAddListeningPort(servicePort)), 0, true);
        TAutoPtr<IEventHandle> handle;
        runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvConfirmListen>(handle);

        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/blackbox", serverId)), 0, true);
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/v3/groupmembership", serverId)), 0, true);

        TActorId sender = runtime->AllocateEdgeActor();
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);

        NHttp::TEvHttpProxy::TEvHttpIncomingRequest* request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/blackbox?"));
        NHttp::THttpOutgoingResponsePtr httpResponse = request->Request->CreateResponseOK(R"___(
        {"oauth":{"uid":"1110000000011111","token_id":"111111","device_id":"","device_name":"","scope":"startrek:write yt:api stat:all yql:api ydb:api","ctime":"2016-08-24 19:34:05","issue_time":"2018-12-05 19:13:17","expire_time":null,"is_ttl_refreshable":false,"client_id":"111111111111111111111111111111111","client_name":"YQL","client_icon":"https:\/\/avatars.mds.yandex.net\/get-oauth\/11111\/11111111111111111111111111111111-11111111111111111111111111111111\/normal","client_homepage":"https:\/\/yql.yandex-team.ru\/","client_ctime":"2016-02-02 13:46:09","client_is_yandex":false,"xtoken_id":"","meta":""},"uid":{"value":"1110000000011111","lite":false,"hosted":false},"login":"user1","have_password":true,"have_hint":false,"karma":{"value":0},"karma_status":{"value":0},"status":{"value":"VALID","id":0},"error":"OK","connection_id":"t:111111"}
                                                                                          )___", "application/json");
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);

        request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/v3/groupmembership?"));
        httpResponse = request->Request->CreateResponseOK(R"___(
        {
        "pages": 1,
        "total": 1,
        "result": [
          {
            "group": {
              "ancestors": [
                {
                  "url": "group2"
                }
              ],
              "url": "group1"
            },
            "person": {
              "login": "user1"
            }
          }
        ],
        "limit": 1000,
        "page": 1,
        "links": {}
        }
        )___", "application/json");
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token != nullptr);
        UNIT_ASSERT_EQUAL(result->Token->GetUserSID(), "user1@staff");
    }

    Y_UNIT_TEST(BlackBoxAndStaffBad) {
        using namespace Tests;
        TPortManager tp;
        ui16 kikimrPort = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(2280);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(true);
        authConfig.SetBlackBoxEndpoint("http://[::]:" + ToString(servicePort));
        authConfig.SetUseStaff(true);
        authConfig.SetStaffEndpoint("http://[::]:" + ToString(servicePort));
        auto settings = TServerSettings(kikimrPort, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        TString userToken = "user1";

        TActorId httpProxyId = runtime->Register(NHttp::CreateHttpProxy());
        TActorId serverId = runtime->AllocateEdgeActor();
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvAddListeningPort(servicePort)), 0, true);
        TAutoPtr<IEventHandle> handle;
        runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvConfirmListen>(handle);

        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/blackbox", serverId)), 0, true);
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/v3/groupmembership", serverId)), 0, true);

        TActorId sender = runtime->AllocateEdgeActor();
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(userToken)), 0);

        NHttp::TEvHttpProxy::TEvHttpIncomingRequest* request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/blackbox?"));
        NHttp::THttpOutgoingResponsePtr httpResponse = request->Request->CreateResponseOK(R"___(
        {"oauth":{"uid":"1110000000011111","token_id":"111111","device_id":"","device_name":"","scope":"startrek:write yt:api stat:all yql:api ydb:api","ctime":"2016-08-24 19:34:05","issue_time":"2018-12-05 19:13:17","expire_time":null,"is_ttl_refreshable":false,"client_id":"111111111111111111111111111111111","client_name":"YQL","client_icon":"https:\/\/avatars.mds.yandex.net\/get-oauth\/11111\/11111111111111111111111111111111-11111111111111111111111111111111\/normal","client_homepage":"https:\/\/yql.yandex-team.ru\/","client_ctime":"2016-02-02 13:46:09","client_is_yandex":false,"xtoken_id":"","meta":""},"uid":{"value":"1110000000011111","lite":false,"hosted":false},"login":"user1","have_password":true,"have_hint":false,"karma":{"value":0},"karma_status":{"value":0},"status":{"value":"VALID","id":0},"error":"OK","connection_id":"t:111111"}
                                                                                          )___", "application/json");
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);

        request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/v3/groupmembership?"));
        httpResponse = request->Request->CreateResponseGatewayTimeout();
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT_VALUES_EQUAL(result->Error.Message, "Error reading Staff response for user 1110000000011111: 504");
        UNIT_ASSERT(result->Error.Retryable);
        UNIT_ASSERT(result->Token == nullptr);
    }

    Y_UNIT_TEST(BlackBoxAndStaffAuthorizeGood) {
        using namespace Tests;
        TPortManager tp;
        ui16 kikimrPort = tp.GetPort(2134);
        ui16 grpcPort = tp.GetPort(2135);
        ui16 servicePort = tp.GetPort(2280);
        TString accessServiceEndpoint = "localhost:" + ToString(servicePort);
        NKikimrProto::TAuthConfig authConfig;
        authConfig.SetUseBlackBox(true);
        authConfig.SetBlackBoxEndpoint("http://[::]:" + ToString(servicePort));
        authConfig.SetUseStaff(true);
        authConfig.SetStaffEndpoint("http://[::]:" + ToString(servicePort));
        auto settings = TServerSettings(kikimrPort, authConfig);
        settings.SetDomainName("Root");
        settings.CreateTicketParser = NKikimrYandex::CreateTicketParser;
        TServer server(settings);
        server.EnableGRpc(grpcPort);
        server.GetRuntime()->SetLogPriority(NKikimrServices::TICKET_PARSER, NLog::PRI_TRACE);
        server.GetRuntime()->SetLogPriority(NKikimrServices::GRPC_CLIENT, NLog::PRI_TRACE);
        TClient client(settings);
        NClient::TKikimr kikimr(client.GetClientConfig());
        client.InitRootScheme();
        TTestActorRuntime* runtime = server.GetRuntime();

        TString userToken = "user1";

        TActorId httpProxyId = runtime->Register(NHttp::CreateHttpProxy());
        TActorId serverId = runtime->AllocateEdgeActor();
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvAddListeningPort(servicePort)), 0, true);
        TAutoPtr<IEventHandle> handle;
        runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvConfirmListen>(handle);

        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/blackbox", serverId)), 0, true);
        runtime->Send(new NActors::IEventHandle(httpProxyId, serverId, new NHttp::TEvHttpProxy::TEvRegisterHandler("/v3/groupmembership", serverId)), 0, true);

        TActorId sender = runtime->AllocateEdgeActor();
        runtime->Send(new IEventHandle(MakeTicketParserID(), sender, new TEvTicketParser::TEvAuthorizeTicket(userToken, {{"cloud_id", "xxxx"}}, {"permission1"})), 0);

        NHttp::TEvHttpProxy::TEvHttpIncomingRequest* request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/blackbox?"));
        NHttp::THttpOutgoingResponsePtr httpResponse = request->Request->CreateResponseOK(R"___(
        {"oauth":{"uid":"1110000000011111","token_id":"111111","device_id":"","device_name":"","scope":"startrek:write yt:api stat:all yql:api ydb:api","ctime":"2016-08-24 19:34:05","issue_time":"2018-12-05 19:13:17","expire_time":null,"is_ttl_refreshable":false,"client_id":"111111111111111111111111111111111","client_name":"YQL","client_icon":"https:\/\/avatars.mds.yandex.net\/get-oauth\/11111\/11111111111111111111111111111111-11111111111111111111111111111111\/normal","client_homepage":"https:\/\/yql.yandex-team.ru\/","client_ctime":"2016-02-02 13:46:09","client_is_yandex":false,"xtoken_id":"","meta":""},"uid":{"value":"1110000000011111","lite":false,"hosted":false},"login":"user1","have_password":true,"have_hint":false,"karma":{"value":0},"karma_status":{"value":0},"status":{"value":"VALID","id":0},"error":"OK","connection_id":"t:111111"}
                                                                                          )___", "application/json");
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);

        request = runtime->GrabEdgeEvent<NHttp::TEvHttpProxy::TEvHttpIncomingRequest>(handle);
        UNIT_ASSERT(request->Request->URL.StartsWith("/v3/groupmembership?"));
        httpResponse = request->Request->CreateResponseOK(R"___(
        {
        "pages": 1,
        "total": 1,
        "result": [
          {
            "group": {
              "ancestors": [
                {
                  "url": "group2"
                }
              ],
              "url": "group1"
            },
            "person": {
              "login": "user1"
            }
          }
        ],
        "limit": 1000,
        "page": 1,
        "links": {}
        }
        )___", "application/json");
        runtime->Send(new NActors::IEventHandle(handle->Sender, serverId, new NHttp::TEvHttpProxy::TEvHttpOutgoingResponse(httpResponse)), 0, true);

        TEvTicketParser::TEvAuthorizeTicketResult* result = runtime->GrabEdgeEvent<TEvTicketParser::TEvAuthorizeTicketResult>(handle);
        UNIT_ASSERT(result->Error.empty());
        UNIT_ASSERT(result->Token != nullptr);
        UNIT_ASSERT_EQUAL(result->Token->GetUserSID(), "user1@staff");
    }
}
}
