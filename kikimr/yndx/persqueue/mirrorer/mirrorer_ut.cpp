#include "reader_factory.h"

#include <ydb/public/sdk/cpp/client/ydb_persqueue_core/ut/ut_utils/test_server.h>
#include <ydb/public/sdk/cpp/client/ydb_persqueue_core/ut/ut_utils/data_plane_helpers.h>

#include <ydb/library/persqueue/tests/counters.h>

#include <library/cpp/testing/unittest/registar.h>

namespace NKikimr::NPersQueueTests {

Y_UNIT_TEST_SUITE(TYndxPersQueueMirrorer) {
    void CheckIncorrectCreds(const NKikimrPQ::TMirrorPartitionConfig::TCredentials& creds) {
        NPersQueue::TTestServer server(PQSettings(0, 1));
        server.EnableLogs({
            NKikimrServices::PQ_WRITE_PROXY, NKikimrServices::PQ_READ_PROXY, NKikimrServices::PQ_MIRRORER
        });

        const auto& settings = server.CleverServer->GetRuntime()->GetAppData().PQConfig.GetMirrorConfig().GetPQLibSettings();

        auto fabric = std::make_shared<NKikimr::NPQ::TYndxPersQueueMirrorReaderFactory>();
        fabric->Initialize(server.CleverServer->GetRuntime()->GetAnyNodeActorSystem(), settings);
        for (ui32 nodeId = 0; nodeId < server.CleverServer->GetRuntime()->GetNodeCount(); ++nodeId) {
            server.CleverServer->GetRuntime()->GetAppData(nodeId).PersQueueMirrorReaderFactory = fabric.get();
        }

        ui32 partitionsCount = 1;
        TString srcTopic = "acc/topic2";
        TString dstTopic = "acc/topic1";
        TString srcTopicFullName = NPersQueue::BuildFullTopicName(srcTopic, "dc1");
        TString dstTopicFullName = NPersQueue::BuildFullTopicName(dstTopic, "dc1");

        server.AnnoyingClient->CreateTopic(srcTopicFullName, partitionsCount);

        NKikimrPQ::TMirrorPartitionConfig mirrorFrom;
        mirrorFrom.SetEndpoint("localhost");
        mirrorFrom.SetEndpointPort(server.GrpcPort);
        mirrorFrom.SetTopic(srcTopic);
        mirrorFrom.SetConsumer("some_user");
        mirrorFrom.SetSyncWriteTime(true);
        mirrorFrom.MutableCredentials()->CopyFrom(creds);

        server.AnnoyingClient->CreateTopic(
            dstTopicFullName,
            partitionsCount,
            /*ui32 lowWatermark =*/ 8_MB,
            /*ui64 lifetimeS =*/ 86400,
            /*ui64 writeSpeed =*/ 20000000,
            /*TString user =*/ "",
            /*ui64 readSpeed =*/ 200000000,
            /*TVector<TString> rr =*/ {},
            /*TVector<TString> important =*/ {},
            mirrorFrom
        );

        auto driver = server.AnnoyingClient->GetDriver();

        // write to source topic
        {
            auto writer = CreateSimpleWriter(*driver, srcTopic, "some_source_id");
            ui64 seqNo = writer->GetInitSeqNo();

            for (ui32 i = 1; i <= 11; ++i) {
                auto res = writer->Write(TString(i, 'a'), ++seqNo);
                UNIT_ASSERT(res);
            }
            auto res = writer->Close(TDuration::Seconds(10));
            UNIT_ASSERT(res);
        }
        {
            auto reader = CreateReader(
                *driver,
                NYdb::NPersQueue::TReadSessionSettings()
                    .AppendTopics(dstTopic)
                    .ConsumerName("shared/user")
                    .ReadOnlyOriginal(true)
            );
            auto dataEvent = GetNextMessageSkipAssignment(reader, TDuration::Seconds(10));
            UNIT_ASSERT(!dataEvent);
        }
        {
            auto counters =  GetCountersLegacy(server.CleverServer->GetRuntime()->GetMonPort(), "pqproxy", "writeSession", dstTopic, "", "Dc1", "", "");
            std::optional<i64> mirrorerErrorsRemoteValue;
            for (auto& sensor : counters["sensors"].GetArray()) {
                if (sensor["labels"]["sensor"] == "MirrorerErrorsRemote") {
                    mirrorerErrorsRemoteValue = sensor["value"].GetInteger();
                }
            }
            UNIT_ASSERT(mirrorerErrorsRemoteValue);
            UNIT_ASSERT_VALUES_UNEQUAL(mirrorerErrorsRemoteValue.value(), 0);
        }
    }

    Y_UNIT_TEST(TestWithoutReadRights) {
        NKikimrPQ::TMirrorPartitionConfig::TCredentials creds;
        creds.SetOauthToken("user@" BUILTIN_ACL_DOMAIN);
        CheckIncorrectCreds(creds);
    }

    Y_UNIT_TEST(TestIncorrectJwt) {
        NKikimrPQ::TMirrorPartitionConfig::TCredentials creds;
        creds.SetJwtParams("it's not json");
        CheckIncorrectCreds(creds);
    }
}

} // NKikimr::NPersQueueTests
