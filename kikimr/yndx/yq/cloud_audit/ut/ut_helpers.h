#pragma once

#include <ydb/core/testlib/basics/appdata.h>
#include <ydb/core/testlib/basics/runtime.h>
#include <kikimr/yndx/yq/cloud_audit/yq_cloud_audit_service.h>
#include <ydb/core/yq/libs/audit/yq_audit_service.h>

#include <ydb/library/folder_service/folder_service.h>
#include <ydb/library/folder_service/mock/mock_folder_service.h>

#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/actorid.h>
#include <library/cpp/actors/core/event.h>
#include <library/cpp/testing/unittest/registar.h>

namespace NYq {
    struct YqAuditServiceSetup {
        YqAuditServiceSetup(TString uri) : Runtime(new NActors::TTestBasicRuntime(1, true)) {
            NConfig::TAuditConfig config;
            config.set_enabled(true);
            config.mutable_uaconfig()->SetUri(uri);

            Runtime->AddLocalService(
                YqAuditServiceActorId(),
                NActors::TActorSetupCmd(
                    CreateYqCloudAuditServiceActor(config),
                    NActors::TMailboxType::Simple,
                    0));

            NKikimrProto::NFolderService::TFolderServiceConfig folderServiceConfig;
            Runtime->AddLocalService(
                NKikimr::NFolderService::FolderServiceActorId(),
                NActors::TActorSetupCmd(
                    NKikimr::NFolderService::CreateMockFolderServiceActor(folderServiceConfig),
                    NActors::TMailboxType::Simple,
                    0));

            TAutoPtr<NKikimr::TAppPrepare> app = new NKikimr::TAppPrepare();
            Runtime->Initialize(app->Unwrap());

            EdgeId = Runtime->AllocateEdgeActor();
        }

        void Send(NActors::IEventBase* event) {
            Runtime->Send(new NActors::IEventHandle(YqAuditServiceActorId(), EdgeId, event));
        }

        NActors::TActorId EdgeId;
        std::unique_ptr<NActors::TTestActorRuntime> Runtime;
    };
}
