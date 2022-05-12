#include "export.h"
#include "mon.h"
#include "query_replay.h"
#include "sqs.h"

#include <ydb/core/driver_lib/run/main.h>
#include <kikimr/yndx/grpc_services/yndx_rate_limiter/grpc_service.h>
#include <kikimr/yndx/grpc_services/persqueue/persqueue.h>
#include <kikimr/yndx/grpc_services/yndx_keyvalue/grpc_service.h>
#include <kikimr/yndx/persqueue/mirrorer/reader_factory.h>
#include <kikimr/yndx/sqs/auth_factory.h>
#include <kikimr/yndx/log_backend/log_backend.h>
#include <kikimr/yndx/security/ticket_parser.h>
#include <kikimr/yndx/security/ydb_credentials_provider_factory.h>
#include <kikimr/yndx/yq/cloud_audit/yq_cloud_audit_service.h>
#include <ydb/library/folder_service/folder_service.h>
#include <kikimr/library/pdisk_io_spdk/aio_spdk.h>
#include <ydb/core/client/server/msgbus_server_pq_metacache.h>
#include <kikimr/yndx/persqueue/msgbus_server/read_session_info.h>

#include <kikimr/yndx/http_proxy/auth_factory.h>

#include <ydb/library/yql/providers/pq/cm_client/lib/client.h>

int main(int argc, char **argv) {
    SetupTerminateHandler();

    auto factories = std::make_shared<NKikimr::TModuleFactories>();
    factories->LogBackendFactory = std::make_shared<NKikimr::TLogBackendFactoryWithUnifiedAgent>();
    factories->QueryReplayBackendFactory = std::make_shared<NKikimr::TQueryReplayBackendFactory>();
    factories->PqCmConnections = MakeIntrusive<NPq::NConfigurationManager::TConnections>();
    factories->DataShardExportFactory = std::make_shared<TDataShardExportFactory>();
    factories->SqsEventsWriterFactory = std::make_shared<TSqsEventsWriterFactory>();
    factories->PersQueueMirrorReaderFactory = std::make_shared<NKikimr::NPQ::TYndxPersQueueMirrorReaderFactory>();
    factories->CreateTicketParser = NKikimrYandex::CreateTicketParser;
    factories->FolderServiceFactory = NKikimr::NFolderService::CreateFolderServiceActor;
    factories->YqAuditServiceFactory = NYq::CreateYqCloudAuditServiceActor;
    factories->YdbCredentialProviderFactory = NKikimr::CreateYndxYdbCredentialsProviderFactory;
    factories->IoContextFactory = std::make_shared<NKikimr::NPDisk::TIoContextFactorySPDK>();
    factories->MonitoringFactory = NKikimr::MonitoringFactory;
    factories->SqsAuthFactory = std::make_shared<NKikimr::NSQS::TMultiAuthFactory>();
    factories->DataStreamsAuthFactory = std::make_shared<NKikimr::NHttpProxy::TIamAuthFactory>();
    factories->PQReadSessionsInfoWorkerFactory = std::make_shared<NKikimr::NMsgBusProxy::TPersQueueGetReadSessionsInfoWorkerWithPQv0Factory>();

    factories->GrpcServiceFactory.Register<NKikimr::NQuoter::TYndxRateLimiterGRpcService>("rate_limiter");
    factories->GrpcServiceFactory.Register<NKikimr::NGRpcService::TGRpcPersQueueService>(
        "pq",
        false,
        NKikimr::NMsgBusProxy::CreatePersQueueMetaCacheV2Id()
    );
    factories->GrpcServiceFactory.Register<NKikimr::NGRpcService::TYndxKeyValueGRpcService>("keyvalue");

    return ParameterizedMain(argc, argv, std::move(factories));
}
