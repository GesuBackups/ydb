#include "ut_helpers.h"

#include <ydb/core/yq/libs/audit/events/events.h>
#include <ydb/core/yq/libs/audit/yq_audit_service.h>

#include <logbroker/unified_agent/common/proc_stat.h>
#include <logbroker/unified_agent/tests/integration/lib/agent_test_base.h>

#include <library/cpp/actors/core/actor.h>
#include <library/cpp/testing/common/env.h>

#include <util/system/env.h>

#include <memory>

namespace NYq {

class UASenderTest: public NUnifiedAgent::TAgentTestBase {
private:
    UNIT_TEST_SUITE(UASenderTest);
    UNIT_TEST(TestRecipe);
    UNIT_TEST_SUITE_END();

    void SetUp() override {
        NUnifiedAgent::TAgentTestBase::SetUp();

        {
            const auto pid = FromString<ui64>(GetEnv("UA_RECIPE_PID"));
            UNIT_ASSERT(NUnifiedAgent::TProcStat::CheckIfProcessExists(pid));
        }

        Port = FromString<ui16>(GetEnv("UA_RECIPE_PORT"));
        StatusPort = FromString<ui16>(GetEnv("UA_RECIPE_STATUS_PORT"));
        ClientParameters->Uri = Sprintf("localhost:%u", Port);

        AuditServiceSetupPtr = std::make_unique<YqAuditServiceSetup>(ClientParameters->Uri);
    }

private:
    std::unique_ptr<YqAuditServiceSetup> AuditServiceSetupPtr;

public:
    void TestRecipe() {
        YandexQuery::Connection connection;
        connection.mutable_content()->set_name("conn_name1");
        connection.mutable_content()->mutable_acl()->set_visibility(::YandexQuery::Acl_Visibility::Acl_Visibility_PRIVATE);
        connection.mutable_content()->mutable_setting()->mutable_object_storage()->set_bucket("bucket");
        connection.mutable_meta()->set_id("conn_id1");
        connection.mutable_meta()->mutable_created_at()->set_seconds(321534);

        TAuditDetails<YandexQuery::Connection> details;
        details.After = connection;

        TEvAuditService::TExtraInfo extraInfo {
            .CloudId = "cloudId1",
            .FolderId = "folderId1",
            .PeerName = "ipv6:[2a02:6b8:c0e:501:0:fc58:a5d3:2b8]:56915"
        };

        YandexQuery::CreateConnectionRequest request;
        request.mutable_content()->set_name("name1");
        request.mutable_content()->mutable_acl()->set_visibility(YandexQuery::Acl::Visibility::Acl_Visibility_SCOPE);
        request.mutable_content()->mutable_setting()->mutable_monitoring()->set_cluster("cluster1");

        NYql::TIssues issues;

        AuditServiceSetupPtr->Send(new TEvAuditService::CreateConnectionAuditReport(
            std::move(extraInfo),
            request,
            issues,
            details,
            "eventId1"));

        {
            const auto recipeOutputDir = GetActiveOutputPath() / "ua_recipe_working_dir/data/output";
            const auto filePath = recipeOutputDir / "output1";

            const auto expectedPath = TFsPath(ArcadiaSourceRoot()) / "kikimr/yndx/yq/cloud_audit/ut/data/output1.json";

            NUnifiedAgent::WaitOutputJson(filePath, NUnifiedAgent::ReadEntireFile(expectedPath), "\n==1==\n");
        }

        {
            const auto agentCounters = QueryAgentCounters();
            const auto ackMessages = agentCounters.GetOutgoingFlowCounter("test_grpc_input", "AckMessages");
            UNIT_ASSERT_VALUES_EQUAL(ackMessages, 1);
        }
    }


};

UNIT_TEST_SUITE_REGISTRATION(UASenderTest);

}
