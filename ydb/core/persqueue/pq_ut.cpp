#include "pq_ut.h"

#include <ydb/core/testlib/basics/runtime.h>
#include <ydb/core/tablet_flat/tablet_flat_executed.h>
#include <ydb/core/tx/schemeshard/schemeshard.h>
#include <ydb/public/lib/base/msgbus.h>
#include <ydb/core/keyvalue/keyvalue_events.h>
#include <ydb/core/persqueue/events/global.h>
#include <ydb/core/tablet/tablet_counters_aggregator.h>
#include <ydb/core/persqueue/partition.h>
#include <ydb/core/engine/minikql/flat_local_tx_factory.h>
#include <ydb/core/security/ticket_parser.h>

#include <ydb/core/testlib/fake_scheme_shard.h>
#include <ydb/core/testlib/tablet_helpers.h>

#include <library/cpp/testing/unittest/registar.h>

#include <util/system/sanitizers.h>
#include <util/system/valgrind.h>


namespace NKikimr {
const static TString TOPIC_NAME = "rt3.dc1--topic";
Y_UNIT_TEST_SUITE(TPQTest) {


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SINGLE COMMAND TEST FUNCTIONS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST(TestGroupsBalancer) {
    TTestContext tc;
    TFinalizer finalizer(tc);
    tc.Prepare();

    tc.Runtime->SetScheduledLimit(50);
    tc.Runtime->SetDispatchTimeout(TDuration::Seconds(1));
    tc.Runtime->SetLogPriority(NKikimrServices::PERSQUEUE, NLog::PRI_DEBUG);
    TFakeSchemeShardState::TPtr state{new TFakeSchemeShardState()};
    ui64 ssId = 325;
    BootFakeSchemeShard(*tc.Runtime, ssId, state);

    BalancerPrepare(TOPIC_NAME, {{0,{1, 1}}, {11,{1, 1}}, {1,{1, 2}}, {2,{1, 2}}}, ssId, tc);

    TActorId pipe = RegisterReadSession("session1", tc);
    Y_UNUSED(pipe);
    WaitPartition("session1", tc, 0, "", "", TActorId());
    WaitPartition("session1", tc, 0, "", "", TActorId());
    WaitPartition("session1", tc, 0, "", "", TActorId());
    WaitPartition("session1", tc, 0, "", "", TActorId());
    WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions - return error

    TActorId pipe2 = RegisterReadSession("session2", tc, {1});

    WaitPartition("session2", tc, 0, "", "", TActorId());
    WaitPartition("session2", tc, 0, "", "", TActorId());
    WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions to balance

    TActorId pipe4 = RegisterReadSession("session8", tc, {1});
    Y_UNUSED(pipe4);

    WaitPartition("session8", tc, 0, "session2", "topic1", pipe2);
    WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions to balance

    tc.Runtime->Send(new IEventHandle(pipe2, tc.Edge, new TEvents::TEvPoisonPill()), 0, true); //will cause dying of pipe and first session

    WaitPartition("session8", tc, 0, "", "", TActorId());
    WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions to balance

    RegisterReadSession("session3", tc);
    WaitPartition("session3", tc, 0, "", "", TActorId());
    WaitPartition("session3", tc, 0, "", "", TActorId());
    WaitPartition("session3", tc, 0, "session8", "topic1", pipe4);
    WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions to balance


}

Y_UNIT_TEST(TestGroupsBalancer2) {
    TTestContext tc;
    TFinalizer finalizer(tc);
    tc.Prepare();

    tc.Runtime->SetScheduledLimit(50);
    tc.Runtime->SetDispatchTimeout(TDuration::Seconds(1));
    tc.Runtime->SetLogPriority(NKikimrServices::PERSQUEUE, NLog::PRI_DEBUG);
    TFakeSchemeShardState::TPtr state{new TFakeSchemeShardState()};
    ui64 ssId = 325;
    BootFakeSchemeShard(*tc.Runtime, ssId, state);

    BalancerPrepare(TOPIC_NAME, {{0, {1, 1}}, {1, {1, 2}}, {2, {1, 3}}, {3, {1, 4}}}, ssId, tc);

    TActorId pipe = RegisterReadSession("session1", tc, {1,2});
    Y_UNUSED(pipe);

    WaitPartition("session1", tc, 0, "", "", TActorId());
    WaitPartition("session1", tc, 0, "", "", TActorId());
    WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions - return error
    TActorId pipe2 = RegisterReadSession("session2", tc, {3,4});
    Y_UNUSED(pipe2);

    WaitPartition("session2", tc, 0, "", "", TActorId());
    WaitPartition("session2", tc, 0, "", "", TActorId());
    WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions - return error
}

Y_UNIT_TEST(TestGroupsBalancer3) {
    TTestContext tc;
    TFinalizer finalizer(tc);
    tc.Prepare();

    tc.Runtime->SetScheduledLimit(50);
    tc.Runtime->SetDispatchTimeout(TDuration::Seconds(1));
    tc.Runtime->SetLogPriority(NKikimrServices::PERSQUEUE, NLog::PRI_DEBUG);
    TFakeSchemeShardState::TPtr state{new TFakeSchemeShardState()};
    ui64 ssId = 325;
    BootFakeSchemeShard(*tc.Runtime, ssId, state);

    BalancerPrepare(TOPIC_NAME, {{0, {1, 1}}, {1, {1, 2}} }, ssId, tc);

    TActorId pipe = RegisterReadSession("session", tc, {2});

    WaitPartition("session", tc, 0, "", "", TActorId());
    WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions - return error

    tc.Runtime->Send(new IEventHandle(pipe, tc.Edge, new TEvents::TEvPoisonPill()), 0, true); //will cause dying of pipe and first session

    TActorId pipe2 = RegisterReadSession("session1", tc);
    Y_UNUSED(pipe2);

    WaitPartition("session1", tc, 0, "", "", TActorId());
    WaitPartition("session1", tc, 0, "", "", TActorId());
    WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions - return error

    pipe = RegisterReadSession("session2", tc, {2});
    WaitSessionKill(tc); //session 1 will die
}


Y_UNIT_TEST(TestUserInfoCompatibility) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        TString client = "test";
        tc.Runtime->SetLogPriority(NKikimrServices::PERSQUEUE, NLog::PRI_DEBUG);

        PQTabletPrepare(20000000, 100_MB, 0, {{client, false}}, tc, 4, 6_MB, true, 0, 0, 1);

        TVector<std::pair<ui64, TString>> data;
        data.push_back({1, "s"});
        data.push_back({2, "q"});
        CmdWrite(0, "sourceid", data, tc);
        CmdWrite(1, "sourceid", data, tc);
        CmdWrite(2, "sourceid", data, tc);
        CmdWrite(3, "sourceid", data, tc);


        THolder<TEvKeyValue::TEvRequest> request(new TEvKeyValue::TEvRequest);
        FillUserInfo(request->Record.AddCmdWrite(), client, 0, 0);
        FillDeprecatedUserInfo(request->Record.AddCmdWrite(), client, 0, 0);
        FillUserInfo(request->Record.AddCmdWrite(), client, 1, 1);
        FillDeprecatedUserInfo(request->Record.AddCmdWrite(), client, 2, 1);
        FillUserInfo(request->Record.AddCmdWrite(), client, 2, 1);
        FillDeprecatedUserInfo(request->Record.AddCmdWrite(), client, 3, 0);
        FillUserInfo(request->Record.AddCmdWrite(), client, 3, 1);

        tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
        TAutoPtr<IEventHandle> handle;
        TEvKeyValue::TEvResponse* result = tc.Runtime->GrabEdgeEvent<TEvKeyValue::TEvResponse>(handle);
        Y_UNUSED(result);

        RestartTablet(tc);
        Cerr  << "AFTER RESTART\n";

        CmdGetOffset(0, client, 0, tc);
        CmdGetOffset(1, client, 1, tc);
        CmdGetOffset(2, client, 1, tc);
        CmdGetOffset(3, client, 1, tc);

    });
}

Y_UNIT_TEST(TestReadRuleVersions) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        TString client = "test";

        PQTabletPrepare(20000000, 100_MB, 0, {{client, false}, {"another-user", false}}, tc, 3);

        tc.Runtime->SetLogPriority(NKikimrServices::PERSQUEUE, NLog::PRI_DEBUG);

        TVector<std::pair<ui64, TString>> data;
        data.push_back({1, "s"});
        data.push_back({2, "q"});
        CmdWrite(0, "sourceid", data, tc);
        CmdWrite(1, "sourceid", data, tc);
        CmdWrite(2, "sourceid", data, tc);

        CmdSetOffset(0, client, 1, false, tc);
        CmdSetOffset(1, client, 2, false, tc);

        {
            THolder<TEvKeyValue::TEvRequest> request(new TEvKeyValue::TEvRequest);

            FillUserInfo(request->Record.AddCmdWrite(), "old_consumer", 0, 0);
            FillDeprecatedUserInfo(request->Record.AddCmdWrite(), "old_consumer", 0, 0);

            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            TAutoPtr<IEventHandle> handle;
            TEvKeyValue::TEvResponse* result = tc.Runtime->GrabEdgeEvent<TEvKeyValue::TEvResponse>(handle);
            Y_UNUSED(result);

        }

        RestartTablet(tc);

        CmdGetOffset(0, client, 1, tc);
        CmdGetOffset(1, client, 2, tc);
        CmdGetOffset(0, "user", 0, tc);

        {
            THolder<TEvKeyValue::TEvRequest> request(new TEvKeyValue::TEvRequest);
            auto read = request->Record.AddCmdReadRange();
            auto range = read->MutableRange();
            NPQ::TKeyPrefix ikeyFrom(NPQ::TKeyPrefix::TypeInfo, 0, NPQ::TKeyPrefix::MarkUser);
            range->SetFrom(ikeyFrom.Data(), ikeyFrom.Size());
            range->SetIncludeFrom(true);
            NPQ::TKeyPrefix ikeyTo(NPQ::TKeyPrefix::TypeInfo, 1, NPQ::TKeyPrefix::MarkUser);
            range->SetTo(ikeyTo.Data(), ikeyTo.Size());
            range->SetIncludeTo(true);

            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            TAutoPtr<IEventHandle> handle;
            TEvKeyValue::TEvResponse* result = tc.Runtime->GrabEdgeEvent<TEvKeyValue::TEvResponse>(handle);

            Cerr << result->Record << "\n";

            UNIT_ASSERT(result->Record.GetReadRangeResult(0).GetPair().size() == 7);
        }

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc, 3);

        CmdGetOffset(0, client, 0, tc);
        CmdGetOffset(1, client, 0, tc);

        {
            THolder<TEvKeyValue::TEvRequest> request(new TEvKeyValue::TEvRequest);
            auto read = request->Record.AddCmdReadRange();
            auto range = read->MutableRange();
            NPQ::TKeyPrefix ikeyFrom(NPQ::TKeyPrefix::TypeInfo, 0, NPQ::TKeyPrefix::MarkUser);
            range->SetFrom(ikeyFrom.Data(), ikeyFrom.Size());
            range->SetIncludeFrom(true);
            NPQ::TKeyPrefix ikeyTo(NPQ::TKeyPrefix::TypeInfo, 1, NPQ::TKeyPrefix::MarkUser);
            range->SetTo(ikeyTo.Data(), ikeyTo.Size());
            range->SetIncludeTo(true);

            tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
            TAutoPtr<IEventHandle> handle;
            TEvKeyValue::TEvResponse* result = tc.Runtime->GrabEdgeEvent<TEvKeyValue::TEvResponse>(handle);

            Cerr << result->Record << "\n";

            UNIT_ASSERT(result->Record.GetReadRangeResult(0).GetPair().size() == 3);
        }

        tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, new NActors::NMon::TEvRemoteHttpInfo(TStringBuilder() << "localhost:8765/tablets/app?TabletID=" << tc.TabletId), 0, GetPipeConfigWithRetries());
        TAutoPtr<IEventHandle> handle;

        tc.Runtime->GrabEdgeEvent<NMon::TEvRemoteHttpInfoRes>(handle);
        TString rs = handle->Get<NMon::TEvRemoteHttpInfoRes>()->Html;
        Cerr << rs << "\n";
    });
}

Y_UNIT_TEST(TestCreateBalancer) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(50);
        tc.Runtime->SetDispatchTimeout(TDuration::MilliSeconds(100));

        TFakeSchemeShardState::TPtr state{new TFakeSchemeShardState()};
        ui64 ssId = 325;
        BootFakeSchemeShard(*tc.Runtime, ssId, state);

        BalancerPrepare(TOPIC_NAME, {{1,{1,2}}}, ssId, tc);

        TActorId pipe1 = RegisterReadSession("session0", tc, {1});

        BalancerPrepare(TOPIC_NAME, {{1,{1,2}}, {2,{1,3}}}, ssId, tc);

        tc.Runtime->Send(new IEventHandle(pipe1, tc.Edge, new TEvents::TEvPoisonPill()), 0, true); //will cause dying of pipe and first session


//        BalancerPrepare(TOPIC_NAME, {{2,1}}, tc); //TODO: not supported yet
//        BalancerPrepare(TOPIC_NAME, {{1,1}}, tc); // TODO: not supported yet
        BalancerPrepare(TOPIC_NAME, {{1,{1, 2}}, {2,{1, 3}}, {3,{1, 4}}}, ssId, tc);
        activeZone = false;

        TActorId pipe = RegisterReadSession("session1", tc);
        WaitPartition("session1", tc, 0, "", "", TActorId());
        WaitPartition("session1", tc, 0, "", "", TActorId());
        WaitPartition("session1", tc, 0, "", "", TActorId());
        WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions - return error
        TActorId pipe2 = RegisterReadSession("session2", tc);
        Y_UNUSED(pipe2);
        WaitPartition("session2", tc, 1, "session1", "topic1", pipe);
        WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions to balance
        tc.Runtime->Send(new IEventHandle(pipe, tc.Edge, new TEvents::TEvPoisonPill()), 0, true); //will cause dying of pipe and first session

        TDispatchOptions options;
        options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTabletPipe::EvServerDisconnected));
        tc.Runtime->DispatchEvents(options);
        WaitPartition("session2", tc, 0, "", "", TActorId());
        WaitPartition("session2", tc, 0, "", "", TActorId());
        WaitPartition("", tc, 0, "", "", TActorId(), false);//no partitions to balance
    });
}

Y_UNIT_TEST(TestDescribeBalancer) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        TFakeSchemeShardState::TPtr state{new TFakeSchemeShardState()};
        ui64 ssId = 9876;
        BootFakeSchemeShard(*tc.Runtime, ssId, state);

        tc.Runtime->SetScheduledLimit(50);
        tc.Runtime->SetDispatchTimeout(TDuration::MilliSeconds(100));
        BalancerPrepare(TOPIC_NAME, {{1,{1, 2}}}, ssId, tc);
        TAutoPtr<IEventHandle> handle;
        tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, new TEvPersQueue::TEvDescribe(), 0, GetPipeConfigWithRetries());
        TEvPersQueue::TEvDescribeResponse* result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvDescribeResponse>(handle);
        UNIT_ASSERT(result);
        auto& rec = result->Record;
        UNIT_ASSERT(rec.HasSchemeShardId() && rec.GetSchemeShardId() == ssId);
        RestartTablet(tc);
        tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, new TEvPersQueue::TEvDescribe(), 0, GetPipeConfigWithRetries());
        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvDescribeResponse>(handle);
        UNIT_ASSERT(result);
        auto& rec2 = result->Record;
        UNIT_ASSERT(rec2.HasSchemeShardId() && rec2.GetSchemeShardId() == ssId);
    });
}

Y_UNIT_TEST(TestCheckACL) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        TFakeSchemeShardState::TPtr state{new TFakeSchemeShardState()};
        ui64 ssId = 9876;
        BootFakeSchemeShard(*tc.Runtime, ssId, state);
        IActor* ticketParser = NKikimr::CreateTicketParser(tc.Runtime->GetAppData().AuthConfig);
        TActorId ticketParserId = tc.Runtime->Register(ticketParser);
        tc.Runtime->RegisterService(NKikimr::MakeTicketParserID(), ticketParserId);

        TAutoPtr<IEventHandle> handle;
        THolder<TEvPersQueue::TEvCheckACL> request(new TEvPersQueue::TEvCheckACL());
        request->Record.SetToken(NACLib::TUserToken("client@" BUILTIN_ACL_DOMAIN, {}).SerializeAsString());
        request->Record.SetOperation(NKikimrPQ::EOperation::READ_OP);
        request->Record.SetUser("client");

        tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());

        tc.Runtime->SetScheduledLimit(600);
        tc.Runtime->SetDispatchTimeout(TDuration::MilliSeconds(100));
        BalancerPrepare(TOPIC_NAME, {{1,{1, 2}}}, ssId, tc);

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(NSchemeShard::TEvSchemeShard::EvDescribeSchemeResult);
            tc.Runtime->DispatchEvents(options);
        }

        TEvPersQueue::TEvCheckACLResponse* result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvCheckACLResponse>(handle);
        auto& rec = result->Record;
        UNIT_ASSERT(rec.GetAccess() == NKikimrPQ::EAccess::DENIED);
        UNIT_ASSERT_VALUES_EQUAL(rec.GetTopic(), TOPIC_NAME);

        state->ACL.AddAccess(NACLib::EAccessType::Allow, NACLib::SelectRow, "client@" BUILTIN_ACL_DOMAIN);

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(NSchemeShard::TEvSchemeShard::EvDescribeSchemeResult);
            tc.Runtime->DispatchEvents(options);
        }

        request.Reset(new TEvPersQueue::TEvCheckACL());
        request->Record.SetToken(NACLib::TUserToken("client@" BUILTIN_ACL_DOMAIN, {}).SerializeAsString());
        request->Record.SetUser("client");
        request->Record.SetOperation(NKikimrPQ::EOperation::READ_OP);

        tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvCheckACLResponse>(handle);
        auto& rec2 = result->Record;
        UNIT_ASSERT_C(rec2.GetAccess() == NKikimrPQ::EAccess::ALLOWED, rec2);

        state->ACL.AddAccess(NACLib::EAccessType::Allow, NACLib::UpdateRow, "client@" BUILTIN_ACL_DOMAIN);

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(NSchemeShard::TEvSchemeShard::EvDescribeSchemeResult);
            tc.Runtime->DispatchEvents(options);
        }

        request.Reset(new TEvPersQueue::TEvCheckACL());
        request->Record.SetToken(NACLib::TUserToken("client@" BUILTIN_ACL_DOMAIN, {}).SerializeAsString());
        request->Record.SetUser("client");
        request->Record.SetOperation(NKikimrPQ::EOperation::WRITE_OP);

        tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvCheckACLResponse>(handle);
        auto& rec3 = result->Record;
        UNIT_ASSERT(rec3.GetAccess() == NKikimrPQ::EAccess::ALLOWED);

        request.Reset(new TEvPersQueue::TEvCheckACL());
        request->Record.SetToken(NACLib::TUserToken("client@" BUILTIN_ACL_DOMAIN, {}).SerializeAsString());
        request->Record.SetUser("client2");
        request->Record.SetOperation(NKikimrPQ::EOperation::WRITE_OP);

        tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvCheckACLResponse>(handle);
        auto& rec9 = result->Record;
        UNIT_ASSERT(rec9.GetAccess() == NKikimrPQ::EAccess::ALLOWED);

        request.Reset(new TEvPersQueue::TEvCheckACL());
        // No auth provided and auth for topic not required
        request->Record.SetOperation(NKikimrPQ::EOperation::WRITE_OP);

        tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvCheckACLResponse>(handle);
        auto& rec5 = result->Record;
        UNIT_ASSERT(rec5.GetAccess() == NKikimrPQ::EAccess::ALLOWED);

        request.Reset(new TEvPersQueue::TEvCheckACL());
        // No auth provided and auth for topic not required
        request->Record.SetOperation(NKikimrPQ::EOperation::READ_OP);

        tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvCheckACLResponse>(handle);
        auto& rec6 = result->Record;
        UNIT_ASSERT(rec6.GetAccess() == NKikimrPQ::EAccess::ALLOWED);

        request.Reset(new TEvPersQueue::TEvCheckACL());
        // No auth provided and auth for topic is required
        request->Record.SetOperation(NKikimrPQ::EOperation::READ_OP);
        request->Record.SetToken("");

        BalancerPrepare(TOPIC_NAME, {{1,{1, 2}}}, ssId, tc, true);
        tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvCheckACLResponse>(handle);
        auto& rec7 = result->Record;
        UNIT_ASSERT(rec7.GetAccess() == NKikimrPQ::EAccess::DENIED);

        request.Reset(new TEvPersQueue::TEvCheckACL());
        // No auth provided and auth for topic is required
        request->Record.SetOperation(NKikimrPQ::EOperation::READ_OP);
        request->Record.SetToken("");

        tc.Runtime->SendToPipe(tc.BalancerTabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());
        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvCheckACLResponse>(handle);
        auto& rec8 = result->Record;
        UNIT_ASSERT(rec8.GetAccess() == NKikimrPQ::EAccess::DENIED);
    });
}


void CheckLabeledCountersResponse(ui32 count, TTestContext& tc, TVector<TString> mustHave = {})
{
    IActor* actor = CreateClusterLabeledCountersAggregatorActor(tc.Edge, TTabletTypes::PersQueue);
    tc.Runtime->Register(actor);

    TAutoPtr<IEventHandle> handle;
    TEvTabletCounters::TEvTabletLabeledCountersResponse *result;
    result = tc.Runtime->GrabEdgeEvent<TEvTabletCounters::TEvTabletLabeledCountersResponse>(handle);
    UNIT_ASSERT(result);
    THashSet<TString> groups;

    Cerr << "Checking with  " << count << " groups:\n";

    for (ui32 i = 0; i < result->Record.LabeledCountersByGroupSize(); ++i) {
        auto& c = result->Record.GetLabeledCountersByGroup(i);
        groups.insert(c.GetGroup());
        Cerr << "Has " << c.GetGroup() << "\n";
    }
    UNIT_ASSERT_VALUES_EQUAL(groups.size(), count);
    for (auto& g : mustHave) {
        UNIT_ASSERT(groups.contains(g));
    }
}

Y_UNIT_TEST(TestSwitchOffImportantFlag) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(600);
        PQTabletPrepare(20000000, 100_MB, 0, {}, tc);

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(TEvTabletCounters::EvTabletAddLabeledCounters);
            tc.Runtime->DispatchEvents(options);
        }

        CheckLabeledCountersResponse(8, tc); //only topic counters

        PQTabletPrepare(20000000, 100_MB, 0, {{"user", true}}, tc);

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(TEvTabletCounters::EvTabletAddLabeledCounters);
            tc.Runtime->DispatchEvents(options);
        }


        CheckLabeledCountersResponse(8, tc, {NKikimr::JoinPath({"user/1", TOPIC_NAME})}); //topic counters + important

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc);

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(TEvTabletCounters::EvTabletAddLabeledCounters);
            tc.Runtime->DispatchEvents(options);
        }

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(TEvTabletCounters::EvTabletAddLabeledCounters);
            tc.Runtime->DispatchEvents(options);
        }

        auto MakeTopics = [&] (const TVector<TString>& users) {
            TVector<TString> res;
            for (const auto& u : users) {
                res.emplace_back(NKikimr::JoinPath({u, TOPIC_NAME}));
            }
            return res;
        };
        CheckLabeledCountersResponse(8, tc, MakeTopics({"user/0"})); //topic counters + not important

        PQTabletPrepare(20000000, 100_MB, 0, {{"user", true}, {"user2", true}}, tc);

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(TEvTabletCounters::EvTabletAddLabeledCounters);
            tc.Runtime->DispatchEvents(options);
        }

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(TEvTabletCounters::EvTabletAddLabeledCounters);
            tc.Runtime->DispatchEvents(options);
        }

        CheckLabeledCountersResponse(11, tc, MakeTopics({"user/1", "user2/1"})); //topic counters + not important

        PQTabletPrepare(20000000, 100_MB, 0, {{"user", true}, {"user2", false}}, tc);

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(TEvTabletCounters::EvTabletAddLabeledCounters);
            tc.Runtime->DispatchEvents(options);
        }

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(TEvTabletCounters::EvTabletAddLabeledCounters);
            tc.Runtime->DispatchEvents(options);
        }

        CheckLabeledCountersResponse(12, tc, MakeTopics({"user/1", "user2/0"}));


        PQTabletPrepare(20000000, 100_MB, 0, {{"user", true}}, tc);

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(TEvTabletCounters::EvTabletAddLabeledCounters);
            tc.Runtime->DispatchEvents(options);
        }

        {
            TDispatchOptions options;
            options.FinalEvents.emplace_back(TEvTabletCounters::EvTabletAddLabeledCounters);
            tc.Runtime->DispatchEvents(options);
        }

        CheckLabeledCountersResponse(8, tc, MakeTopics({"user/1"}));


    });
}


Y_UNIT_TEST(TestSeveralOwners) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc); //no important clients, lifetimeseconds=0 - delete all right now, except last datablob

        TVector<std::pair<ui64, TString>> data;

        TString s{32, 'c'};
        ui32 pp = 4 + 8 + 1 + 9;
        data.push_back({1, s.substr(pp)});
        data.push_back({2, s.substr(pp)});
        TString cookie1 = CmdSetOwner(0, tc, "owner1").first;
        TString cookie2 = CmdSetOwner(0, tc, "owner2").first;
        CmdWrite(0, "sourceid0", data, tc, false, {}, true, cookie1, 0, -1, true);

        CmdWrite(0, "sourceid1", data, tc, false, {}, false, cookie2, 0, -1, true);
        CmdWrite(0, "sourceid2", data, tc, false, {}, false, cookie1, 1, -1, true);

        TString cookie3 = CmdSetOwner(0, tc, "owner1").first;

        CmdWrite(0, "sourceid3", data, tc , true, {}, false, cookie1,  2, -1, true);
    });
}


Y_UNIT_TEST(TestWaitInOwners) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc); //no important clients, lifetimeseconds=0 - delete all right now, except last datablob

        TVector<std::pair<ui64, TString>> data;

        TString s{32, 'c'};
        ui32 pp = 4 + 8 + 1 + 9;
        data.push_back({1, s.substr(pp)});
        data.push_back({2, s.substr(pp)});

        CmdSetOwner(0, tc, "owner", false);
        CmdSetOwner(0, tc, "owner", true); //will break last owner

        TActorId newPipe = SetOwner(0, tc, "owner", false); //this owner will wait

        auto p = CmdSetOwner(0, tc, "owner", true); //will break last owner

        TAutoPtr<IEventHandle> handle;
        TEvPersQueue::TEvResponse *result;
        try {
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);
        } catch (NActors::TSchedulingLimitReachedException) {
            result = nullptr;
        }

        Y_VERIFY(!result); //no answer yet

        CmdSetOwner(0, tc);
        CmdSetOwner(0, tc, "owner2"); //just to be dropped by next command

        WritePartData(0, "sourceid", 12, 1, 1, 5, 20, "value", tc, p.first, 0);

        result = tc.Runtime->GrabEdgeEventIf<TEvPersQueue::TEvResponse>(handle, [](const TEvPersQueue::TEvResponse& ev){
                if (ev.Record.HasPartitionResponse() && ev.Record.GetPartitionResponse().CmdWriteResultSize() > 0 || ev.Record.GetErrorCode() != NPersQueue::NErrorCode::OK)
                    return true;
                return false;
            }); //there could be outgoing reads in TestReadSubscription test

        UNIT_ASSERT(result);
        UNIT_ASSERT(result->Record.HasStatus());
        UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::BAD_REQUEST);

        try {
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);
        } catch (NActors::TSchedulingLimitReachedException) {
            result = nullptr;
        }

        UNIT_ASSERT(result); //ok for newPipe because old owner is dead now
        UNIT_ASSERT(result->Record.HasStatus());
        UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);
        UNIT_ASSERT(result->Record.HasPartitionResponse());
        UNIT_ASSERT(result->Record.GetPartitionResponse().HasCmdGetOwnershipResult());

        SetOwner(0, tc, "owner", false); //will wait

        try {
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);
        } catch (NActors::TSchedulingLimitReachedException) {
            result = nullptr;
        }

        Y_VERIFY(!result); //no answer yet, waiting of dying of old ownership session

        tc.Runtime->Send(new IEventHandle(newPipe, tc.Edge, new TEvents::TEvPoisonPill()), 0, true); //will cause dying of pipe and old session

        TDispatchOptions options;
        options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTabletPipe::EvServerDisconnected));
        tc.Runtime->DispatchEvents(options);

        try {
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);
        } catch (NActors::TSchedulingLimitReachedException) {
            result = nullptr;
        }

        UNIT_ASSERT(result); //now ok
        UNIT_ASSERT(result->Record.HasStatus());
        UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);
        UNIT_ASSERT(result->Record.HasPartitionResponse());
        UNIT_ASSERT(result->Record.GetPartitionResponse().HasCmdGetOwnershipResult());
    });
}




Y_UNIT_TEST(TestReserveBytes) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc); //no important clients, lifetimeseconds=0 - delete all right now, except last datablob

        TVector<std::pair<ui64, TString>> data;

        TString s{32, 'c'};
        ui32 pp = 4 + 8 + 1 + 9;
        data.push_back({1, s.substr(pp)});
        data.push_back({2, s.substr(pp)});
        auto p = CmdSetOwner(0, tc);

        CmdReserveBytes(0, tc, p.first, 0, 20000000, p.second);
        CmdReserveBytes(0, tc, p.first, 1, 20000000, p.second, false, true);

        CmdReserveBytes(0, tc, p.first, 2, 40000000, p.second);

        CmdReserveBytes(0, tc, p.first, 3, 80000000, p.second, true);

        TString cookie = p.first;

        CmdWrite(0, "sourceid0", data, tc, false, {}, true, cookie, 4);

        TAutoPtr<IEventHandle> handle;
        TEvPersQueue::TEvResponse *result;
        try {
            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);
        } catch (NActors::TSchedulingLimitReachedException) {
            result = nullptr;
        }

        UNIT_ASSERT(!result);//no answer yet  40 + 80 > 90

        CmdWrite(0, "sourceid2", data, tc, false, {}, false, cookie, 5);

        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle); //now no inflight - 80 may fit

        UNIT_ASSERT(result);
        UNIT_ASSERT(result->Record.HasStatus());
        UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);

        CmdWrite(0, "sourceid3", data, tc, false, {}, false, cookie, 6);

        CmdReserveBytes(0, tc, p.first, 7, 80000000, p.second);
        p = CmdSetOwner(0, tc);
        CmdReserveBytes(0, tc, p.first, 0, 80000000, p.second);

    });
}




Y_UNIT_TEST(TestMessageNo) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc); //no important clients, lifetimeseconds=0 - delete all right now, except last datablob

        TVector<std::pair<ui64, TString>> data;

        TString s{32, 'c'};
        ui32 pp = 4 + 8 + 1 + 9;
        data.push_back({1, s.substr(pp)});
        data.push_back({2, s.substr(pp)});
        TString cookie = CmdSetOwner(0, tc).first;
        CmdWrite(0, "sourceid0", data, tc, false, {}, true, cookie, 0);

        CmdWrite(0, "sourceid2", data, tc, false, {}, false, cookie, 1);

        WriteData(0, "sourceid1", data, tc, cookie, 2, -1);

        TAutoPtr<IEventHandle> handle;
        TEvPersQueue::TEvResponse *result;
        result = tc.Runtime->GrabEdgeEventIf<TEvPersQueue::TEvResponse>(handle, [](const TEvPersQueue::TEvResponse& ev){
            if (!ev.Record.HasPartitionResponse() || !ev.Record.GetPartitionResponse().HasCmdReadResult())
                return true;
            return false;
        }); //there could be outgoing reads in TestReadSubscription test

        UNIT_ASSERT(result);
        UNIT_ASSERT(result->Record.HasStatus());
        UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);

        UNIT_ASSERT(result->Record.GetPartitionResponse().CmdWriteResultSize() == data.size());
        for (ui32 i = 0; i < data.size(); ++i) {
            UNIT_ASSERT(result->Record.GetPartitionResponse().GetCmdWriteResult(i).HasAlreadyWritten());
            UNIT_ASSERT(result->Record.GetPartitionResponse().GetCmdWriteResult(i).HasOffset());
        }
        for (ui32 i = 0; i < data.size(); ++i) {
            auto res = result->Record.GetPartitionResponse().GetCmdWriteResult(i);
            UNIT_ASSERT(!result->Record.GetPartitionResponse().GetCmdWriteResult(i).GetAlreadyWritten());
        }

        CmdWrite(0, "sourceid3", data, tc , true, {}, false, cookie,  0);
    });
}


Y_UNIT_TEST(TestPartitionedBlobFails) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 200_MB, 0, {{"user1", true}}, tc); //one important client, never delete

        TString ss{50_MB, '_'};
        char k = 0;
        TString s = "";
        s += k;
        s += ss;
        s += char((1) % 256);
        ++k;

        TVector<std::pair<ui64, TString>> data;
        data.push_back({1, s});

        TVector<TString> parts;
        ui32 size = 400_KB;
        ui32 diff = 50;
        for (ui32 pos = 0; pos < s.size();) {
            parts.push_back(s.substr(pos, size - diff));
            pos += size - diff;
        }
        Y_VERIFY(parts.size() > 5);

        CmdWrite(0, "sourceid4", data, tc);
        {
            TString cookie = CmdSetOwner(0, tc).first;

            WritePartDataWithBigMsg(0, "sourceid0", 1, 1, 5, s.size(), parts[1], tc, cookie, 0, 12_MB);
            TAutoPtr<IEventHandle> handle;
            TEvPersQueue::TEvResponse *result;

            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);

            UNIT_ASSERT(result->Record.HasStatus());
            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::BAD_REQUEST);
        }

        PQGetPartInfo(0, 1, tc);
        CmdWrite(0, "sourceid5", data, tc);
        RestartTablet(tc);
        PQGetPartInfo(0, 2, tc);

        ui32 toWrite = 5;
        for (ui32 i = 0; i < 2; ++i) {
            TString cookie = CmdSetOwner(0, tc).first;

            for (ui32 j = 0; j < toWrite + 1; ++j) {
                ui32 k = j;
                if (j == toWrite)
                    k = parts.size() - 1;
                WritePartData(0, "sourceid1", -1, j == toWrite ? 2 : 1, k, parts.size(), s.size(), parts[k], tc, cookie, j);

                TAutoPtr<IEventHandle> handle;
                TEvPersQueue::TEvResponse *result;

                result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

                UNIT_ASSERT(result);

                UNIT_ASSERT(result->Record.HasStatus());
                if ( j == toWrite) {
                    UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::BAD_REQUEST);
                } else {
                    UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);

                    UNIT_ASSERT(result->Record.GetPartitionResponse().CmdWriteResultSize() == 1);
                    UNIT_ASSERT(result->Record.GetPartitionResponse().GetCmdWriteResult(0).HasAlreadyWritten());
                    UNIT_ASSERT(result->Record.GetPartitionResponse().GetCmdWriteResult(0).HasOffset());
                    UNIT_ASSERT(result->Record.GetPartitionResponse().GetCmdWriteResult(0).GetOffset() == 2);

                    auto res = result->Record.GetPartitionResponse().GetCmdWriteResult(0);
                    UNIT_ASSERT(!result->Record.GetPartitionResponse().GetCmdWriteResult(0).GetAlreadyWritten());
                }
            }
            PQGetPartInfo(0, i + 2, tc);
            toWrite = parts.size();
        }
        data.back().second.resize(64_KB);
        CmdWrite(0, "sourceid3", data, tc);
        CmdWrite(0, "sourceid5", data, tc);
        activeZone = true;
        data.back().second.resize(8_MB);
        CmdWrite(0, "sourceid7", data, tc);
        activeZone = false;
        {
            TString cookie = CmdSetOwner(0, tc).first;
            WritePartData(0, "sourceidX", 10, 1, 0, 5, s.size(), parts[1], tc, cookie, 0);

            TAutoPtr<IEventHandle> handle;
            TEvPersQueue::TEvResponse *result;

            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);

            //check that after CmdSetOwner all partial data cleared
            cookie = CmdSetOwner(0, tc).first;
            WritePartData(0, "sourceidX", 12, 1, 0, 5, s.size(), parts[1], tc, cookie, 0);

            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);

            //check gaps
            WritePartData(0, "sourceidX", 15, 1, 1, 5, s.size(), parts[1], tc, cookie, 1);

            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::BAD_REQUEST);

            //check partNo gaps
            cookie = CmdSetOwner(0, tc).first;
            WritePartData(0, "sourceidX", 12, 1, 0, 5, s.size(), parts[1], tc, cookie, 0);

            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);

            //check gaps
            WritePartData(0, "sourceidX", 12, 1, 4, 5, s.size(), parts[1], tc, cookie, 1);

            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::BAD_REQUEST);

            //check very big msg
            cookie = CmdSetOwner(0, tc).first;
            WritePartData(0, "sourceidY", 13, 1, 0, 5, s.size(), TString{10_MB, 'a'}, tc, cookie, 0);

            result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

            UNIT_ASSERT(result);
            UNIT_ASSERT(result->Record.HasStatus());
            UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::BAD_REQUEST);
        }
        RestartTablet(tc);
    });
}

Y_UNIT_TEST(TestAlreadyWritten) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc); //no important clients, lifetimeseconds=0 - delete all right now, except last datablob
        activeZone = true;
        TVector<std::pair<ui64, TString>> data;

        TString s{32, 'c'};
        ui32 pp = 4 + 8 + 1 + 9;
        data.push_back({2, s.substr(pp)});
        data.push_back({1, s.substr(pp)});
        CmdWrite(0, "sourceid0", data, tc, false, {1}); //0 is written, 1 is already written
        data[0].first = 4;
        data[1].first = 3;
        CmdWrite(0, "sourceid0", data, tc, false, {3}); //0 is written, 1 is already written
        CmdWrite(0, "sourceid0", data, tc, false, {3, 4}); //all is already written
    });
}


Y_UNIT_TEST(TestAlreadyWrittenWithoutDeduplication) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc); //no important clients, lifetimeseconds=0 - delete all right now, except last datablob
        TVector<std::pair<ui64, TString>> data;
        activeZone = true;

        TString s{32, 'c'};
        ui32 pp = 4 + 8 + 1 + 9;
        data.push_back({2, s.substr(pp)});
        CmdWrite(0, "sourceid0", data, tc, false, {}, false, "", -1, 0, false, false, true);
        data[0].first = 1;
        CmdWrite(0, "sourceid0", data, tc, false, {}, false, "", -1, 1, false, false, true);
        CmdRead(0, 0, Max<i32>(), Max<i32>(), 2, false, tc, {0, 1});
    });
}


Y_UNIT_TEST(TestWritePQCompact) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc, 2, 8_MB - 512_KB);
                //no important clients, lifetimeseconds=0 - delete all right now, except last datablob

        TVector<std::pair<ui64, TString>> data;

        TString ss{1_MB - 100, '_'};
        TString s1{128_KB, 'a'};
        TString s2{2_KB, 'b'};
        TString s3{32, 'c'};
        ui32 pp = 4 + 8 + 2 + 9;
        for (ui32 i = 0; i < 8; ++i) {
            data.push_back({i + 1, ss.substr(pp)});
        }
        CmdWrite(0, "sourceid0", data, tc, false, {}, true); //now 1 blob
        PQGetPartInfo(0, 8, tc);
        data.clear();
        for (ui32 i = 0; i + s1.size() < 7_MB + 4 * s1.size(); i += s1.size()) {
            data.push_back({i + 1, s1.substr(pp)});
        }
        CmdWrite(0, "sourceid1", data, tc);
        PQGetPartInfo(0, 63 + 4, tc);
        data.clear();
        for (ui32 i = 0; i + s2.size() < s1.size(); i += s2.size()) {
            data.push_back({i + 1, s2.substr(pp)});
        }
        CmdWrite(0, "sourceid2", data, tc);
        PQGetPartInfo(8, 2 * 63 + 4, tc); //first is partial, not counted
        data.clear();
        for (ui32 i = 0; i + s3.size() + 540 < s2.size(); i += s3.size()) {
            data.push_back({i + 1, s3.substr(pp)});
        }
        CmdWrite(0, "sourceid3", data, tc); //now 1 blob and at most one

        PQGetPartInfo(8, 177, tc);
        data.resize(1);
        CmdWrite(0, "sourceid4", data, tc); //now 2 blobs, but delete will be done on next write
        //        PQGetUserInfo("aaa", 0, 8 + 88 * 3 + 1, -1, tc); dont check here, at may be deleted already(on restart OnWakeUp will occure)
        activeZone = true;
        CmdWrite(0, "sourceid5", data, tc); //next message just to force drop, don't wait for WakeUp
        activeZone = false;

        PQGetPartInfo(8, 179, tc);

    });
}


Y_UNIT_TEST(TestWritePQBigMessage) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 1000_MB, 0, {{"user1", true}}, tc, 2, 8_MB - 512_KB); //nothing dropped
                //no important clients, lifetimeseconds=0 - delete all right now, except last datablob

        TVector<std::pair<ui64, TString>> data;

        TString ss{50_MB - 100 - 2, '_'};
        TString s1{400_KB - 2, 'a'};
        ui32 pp = 4 + 8 + 2 + 9;
        char k = 0;
        TString s = "";
        s += k;
        s += ss.substr(pp);
        s += char((1) % 256);
        ++k;
        data.push_back({1, s});

        for (ui32 i = 0; i < 25;++i) {
            TString s = "";
            s += k;
            s += s1.substr(pp);
            s += char((i + 2) % 256);
            ++k;
            data.push_back({i + 2, s});
        }
        s = "";
        s += k;
        s += ss.substr(pp);
        s += char((1000) % 256);
        ++k;
        data.push_back({1000, s});
        CmdWrite(0, "sourceid0", data, tc, false, {}, true);
        PQGetPartInfo(0, 27, tc);

        CmdRead(0, 0, Max<i32>(), Max<i32>(), 1, false, tc);
        CmdRead(0, 1, Max<i32>(), Max<i32>(), 25, false, tc);
        CmdRead(0, 24, Max<i32>(), Max<i32>(), 2, false, tc);
        CmdRead(0, 26, Max<i32>(), Max<i32>(), 1, false, tc);

        activeZone = false;
    });
}


Y_UNIT_TEST(TestWritePQ) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        tc.Runtime->SetScheduledLimit(100);

        PQTabletPrepare(20000000, 100_MB, 0, {{"user", true}}, tc); //important client, lifetimeseconds=0 - never delete

        TVector<std::pair<ui64, TString>> data, data1, data2;
        activeZone = PlainOrSoSlow(true, false);

        TString ss{1_MB, '_'};
        TString s1{128_KB, 'a'};
        TString s2{2_KB, 'b'};
        TString s3{32, 'c'};
        ui32 pp = 4 + 8 + 2 + 9;

        TString sb{15_MB + 512_KB, '_'};
        data.push_back({1, sb.substr(pp)});
        CmdWrite(0,"sourceid0", data, tc, false, {}, true, "", -1, 100);
        activeZone = false;

        PQGetPartInfo(100, 101, tc);

        data1.push_back({1, s3.substr(pp)});
        data1.push_back({2, sb.substr(pp)});
        data2.push_back({1, s2.substr(pp)});
        data2.push_back({2, sb.substr(pp)});
        CmdWrite(0,"sourceid1", data1, tc);
        CmdWrite(0,"sourceid2", data2, tc);

        CmdWrite(0,"sourceid3", data1, tc);
        data.clear();
        data.push_back({1, s1.substr(pp)});
        data.push_back({2, ss.substr(pp)});
        CmdWrite(0,"sourceid4", data, tc);

        TString a1{8_MB - 1_KB, '_'};
        TString a2{2_KB, '_'};
        data.clear();
        data.push_back({1, a1.substr(pp)});
        data1.clear();
        data1.push_back({1, a2.substr(pp)});
        CmdWrite(0,"sourceid5", data, tc);
        CmdWrite(0,"sourceid6", data1, tc);
        CmdWrite(0,"sourceid7", data, tc);
        data.back().first = 4296000000lu;
        CmdWrite(0,"sourceid8", data, tc);
        PQGetPartInfo(100, 113, tc);

        data1.push_back({2, a2.substr(pp)});
        CmdWrite(0,"sourceId9", data1, tc, false, {}, false, "", -1, 1000);
        PQGetPartInfo(100, 1002, tc);

        data1.front().first = 3;
        data1.back().first = 4;

        CmdWrite(0,"sourceId9", data1, tc, false, {}, false, "", -1, 2000);
        PQGetPartInfo(100, 2002, tc);

        activeZone = true;

        data1.push_back(data1.back());
        data1[1].first = 3;
        CmdWrite(0,"sourceId10", data1, tc, false, {}, false, "", -1, 3000);
        PQGetPartInfo(100, 3003, tc);

        activeZone = false;

        CmdWrite(1,"sourceId9", data1, tc, false, {}, false, "", -1, 2000); //to other partition

        data1.clear();
        data1.push_back({1, TString{200, 'a'}});
        for (ui32 i = 1; i <= NUM_WRITES; ++i) {
            data1.front().first = i;
            CmdWrite(1, "sourceidx", data1, tc, false, {}, false, "", -1);
        }

        //read all, check offsets
        CmdRead(0, 111, Max<i32>(), Max<i32>(), 8, false, tc, {111,112,1000,1001,2000,2001,3000,3002});

        //read from gap
        CmdRead(0, 500, Max<i32>(), Max<i32>(), 6, false, tc, {1000,1001,2000,2001,3000,3002});

    });
}


Y_UNIT_TEST(TestSourceIdDropByUserWrites) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc); //no important client, lifetimeseconds=0 - delete right now

        TVector<std::pair<ui64, TString>> data;
        activeZone = true;

        TString ss{32, '_'};

        data.push_back({1, ss});
        CmdWrite(0,"sourceid0", data, tc, false, {}, false, "", -1, 100);

        PQGetPartInfo(100, 101, tc);

        CmdWrite(0,"sourceidx", data, tc, false, {}, false, "", -1, 2000);
        CmdWrite(0,"sourceid1", data, tc, false, {}, false, "", -1, 3000);
        PQGetPartInfo(2000, 3001, tc);
        //fail - already written
        CmdWrite(0,"sourceid0", data, tc, false);
        PQGetPartInfo(2000, 3001, tc);

        tc.Runtime->UpdateCurrentTime(tc.Runtime->GetCurrentTime() + TDuration::Minutes(61));
        CmdWrite(0,"sourceid0", data, tc, false);
        CmdWrite(0,"sourceid0", data, tc, false); //second attempt just to be sure that DropOldSourceId is called after previos write, not only on Wakeup
        //ok, hour waited - record writted twice
        PQGetPartInfo(2000, 3002, tc);
    });
}


Y_UNIT_TEST(TestSourceIdDropBySourceIdCount) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc, 2, 6_MB, true, 0, 3); //no important client, lifetimeseconds=0 - delete right now

        TVector<std::pair<ui64, TString>> data;
        activeZone = true;

        TString ss{32, '_'};

        data.push_back({1, ss});
        CmdWrite(0,"sourceid0", data, tc, false, {}, false, "", -1, 100);
        Cout << "written sourceid0" << Endl;

        PQGetPartInfo(100, 101, tc);

        CmdWrite(0,"sourceidx", data, tc, false, {}, false, "", -1, 2000);
        Cout << "written sourceidx" << Endl;
        CmdWrite(0,"sourceid1", data, tc, false, {}, false, "", -1, 3000);
        Cout << "written sourceid1" << Endl;
        PQGetPartInfo(2000, 3001, tc);
        //fail - already written
        CmdWrite(0,"sourceid0", data, tc, false);
        Cout << "written sourceid0" << Endl;
        PQGetPartInfo(2000, 3001, tc);

        for (ui64 i=0; i < 5; ++i) {
            CmdWrite(0, TStringBuilder() << "sourceid_" << i, data, tc, false, {}, false, "", -1, 3001 + i);
            Cout << "written sourceid_" << i << Endl;
        }
        CmdWrite(0,"sourceid0", data, tc, false);
        Cout << "written sourceid0" << Endl;
        PQGetPartInfo(2000, 3007, tc);
    });
}


Y_UNIT_TEST(TestWriteOffsetWithBigMessage) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {{{"user", true}}}, tc, 3); //important client, lifetimeseconds=0 - never delete

        activeZone = false;

        TVector<std::pair<ui64, TString>> data;

        data.push_back({1, TString{10_MB, 'a'}});
        CmdWrite(1, "sourceIdx", data, tc, false, {}, false, "", -1, 80000);
        data.front().first = 2;
        CmdWrite(1, "sourceIdx", data, tc, false, {}, false, "", -1, 160000);

        data.clear();
        data.push_back({1, TString{100_KB, 'a'}});
        for (ui32 i = 0; i < 100; ++i) {
            data.push_back(data.front());
            data.back().first = i + 2;
        }
        CmdWrite(0, "sourceIdx", data, tc, false, {}, false, "", -1, 80000);
        PQGetPartInfo(80000, 80101, tc);
        data.resize(70);
        CmdWrite(2, "sourceId1", data, tc, false, {}, false, "", -1, 0);
        CmdWrite(2, "sourceId2", data, tc, false, {}, false, "", -1, 80000);
    });
}


Y_UNIT_TEST(TestWriteSplit) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {{"user1", true}}, tc); //never delete
        const ui32 size  = PlainOrSoSlow(2_MB, 1_MB);

        TVector<std::pair<ui64, TString>> data;
        data.push_back({1, TString{size, 'b'}});
        data.push_back({2, TString{size, 'a'}});
        activeZone = PlainOrSoSlow(true, false);
        CmdWrite(0, "sourceIdx", data, tc, false, {}, false, "", -1, 40000);
        RestartTablet(tc);
        activeZone = false;
        PQGetPartInfo(40000, 40002, tc);
    });
}


Y_UNIT_TEST(TestLowWatermark) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc, 2, 2_MB); //no important clients, lifetimeseconds=0 - delete all right now, except last datablob

        TVector<std::pair<ui64, TString>> data;

        ui32 pp = 4 + 8 + 2 + 9;

        TString ss{1_MB, '_'};
        data.push_back({1, ss.substr(pp)});
        data.push_back({2, ss.substr(pp)});
        data.push_back({3, ss.substr(pp)});
        CmdWrite(0,"sourceid0", data, tc, false, {}, true);

        PQTabletPrepare(20000000, 100_MB, 0, {}, tc, 2, 6_MB); //no important clients, lifetimeseconds=0 - delete all right now, except last datablob
        CmdWrite(0,"sourceid1", data, tc, false, {}, false); //first are compacted
        PQGetPartInfo(0, 6, tc);
        CmdWrite(0,"sourceid2", data, tc, false, {}, false); //3 and 6 are compacted
        PQGetPartInfo(3, 9, tc);
        PQTabletPrepare(20000000, 100_MB, 0, {}, tc, 2, 3_MB); //no important clients, lifetimeseconds=0 - delete all right now, except last datablob
        CmdWrite(0,"sourceid3", data, tc, false, {}, false); //3, 6 and 3 are compacted
        data.resize(1);
        CmdWrite(0,"sourceid4", data, tc, false, {}, false); //3, 6 and 3 are compacted
        PQGetPartInfo(9, 13, tc);
    });
}



Y_UNIT_TEST(TestWriteToFullPartition) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        activeZone = false;
        tc.Prepare(dispatchName, setup, activeZone);

        tc.Runtime->SetScheduledLimit(100);

        PQTabletPrepare(11, 100_MB, 0, {}, tc);

        TVector<std::pair<ui64, TString>> data;
        activeZone = PlainOrSoSlow(true, false);

        TString s{32, 'c'};
        ui32 pp = 8 + 4 + 2 + 9;
        for (ui32 i = 0; i < 10; ++i) {
            data.push_back({i + 1, s.substr(pp)});
        }
        CmdWrite(0, "sourceid0", data, tc, false, {}, true); //now 1 blob
        PQTabletPrepare(10, 100_MB, 0, {}, tc);
        PQGetPartInfo(0, 10, tc);
        data.resize(1);
        CmdWrite(0, "sourceid1", data, tc, true);
        PQTabletPrepare(12, 100_MB, 0, {}, tc);
        CmdWrite(0, "sourceid1", data, tc);
        PQTabletPrepare(12, 100, 0, {}, tc);
        CmdWrite(0, "sourceid1", data, tc, true);
    });
}



Y_UNIT_TEST(TestPQPartialRead) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);

        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {{"aaa", true}}, tc); //important client - never delete

        activeZone = false;
        TVector<std::pair<ui64, TString>> data;

        ui32 pp =  4 + 8 + 2 + 9 + 100 + 40; //pp is for size of meta
        TString tmp{1_MB - pp - 2, '-'};
        char k = 0;
        TString ss = "";
        ss += k;
        ss += tmp;
        ss += char(1);
        ++k;
        data.push_back({1, ss});

        CmdWrite(0, "sourceid0", data, tc, false, {}, true); //now 1 blob
        PQGetPartInfo(0, 1, tc);

        CmdRead(0, 0, 1, 1, 1, false, tc);
    });
}


Y_UNIT_TEST(TestPQRead) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);

        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {{"aaa", true}}, tc); //important client - never delete

        activeZone = false;
        TVector<std::pair<ui64, TString>> data;

        ui32 pp =  4 + 8 + 2 + 9 + 100 + 40; //pp is for size of meta
        TString tmp{1_MB - pp - 2, '-'};
        char k = 0;
        for (ui32 i = 0; i < 26_MB;) { //3 full blobs and 2 in head
            TString ss = "";
            ss += k;
            ss += tmp;
            ss += char((i + 1) % 256);
            ++k;
            data.push_back({i + 1, ss});
            i += ss.size() + pp;
        }
        CmdWrite(0, "sourceid0", data, tc, false, {}, true); //now 1 blob
        PQGetPartInfo(0, 26, tc);

        CmdRead(0, 26, Max<i32>(), Max<i32>(), 0, true, tc);

        CmdRead(0, 0, Max<i32>(), Max<i32>(), 25, false, tc);
        CmdRead(0, 0, 10, 100_MB, 10, false, tc);
        CmdRead(0, 9, 1, 100_MB, 1, false, tc);
        CmdRead(0, 23, 3, 100_MB, 3, false, tc);

        CmdRead(0, 3, 1000, 511_KB, 1, false, tc);
        CmdRead(0, 3, 1000, 1_KB, 1, false, tc); //at least one message will be readed always
        CmdRead(0, 25, 1000, 1_KB, 1, false, tc); //at least one message will be readed always, from head

        activeZone = true;
        CmdRead(0, 9, 1000, 3_MB, 3, false, tc);
        CmdRead(0, 9, 1000, 3_MB - 10_KB, 3, false, tc);
        CmdRead(0, 25, 1000, 512_KB, 1, false, tc); //from head
        CmdRead(0, 24, 1000, 512_KB, 1, false, tc); //from head

        CmdRead(0, 23, 1000, 98_MB, 3, false, tc);
    });
}


Y_UNIT_TEST(TestPQSmallRead) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);

        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {{"aaa", true}}, tc); //important client - never delete

        activeZone = false;
        TVector<std::pair<ui64, TString>> data;

        ui32 pp =  4 + 8 + 2 + 9 ; //5 is for 8 blobs for header
        TString tmp{32 - pp - 2, '-'};
        char k = 0;
        TString ss = "";
        ss += k;
        ss += tmp;
        ss += char(1);
        data.push_back({1, ss});
        CmdWrite(0, "sourceid0", data, tc, false, {}, true);
        ++k; data[0].second = TString(1, k) + tmp + char(1);
        CmdWrite(0, "sourceid1", data, tc, false, {}, false);
        ++k; data[0].second = TString(1, k) + tmp + char(1);
        CmdWrite(0, "sourceid2", data, tc, false, {}, false);
        ++k; data[0].second = TString(1, k) + tmp + char(1);
        CmdWrite(0, "sourceid3", data, tc, false, {}, false);
        ++k; data[0].second = TString(1, k) + tmp + char(1);
        CmdWrite(0, "sourceid4", data, tc, false, {}, false);
        PQGetPartInfo(0, 5, tc);

        CmdRead(0, 5, Max<i32>(), Max<i32>(), 0, true, tc);
        CmdRead(0, 0, Max<i32>(), Max<i32>(), 5, false, tc);
        CmdRead(0, 0, 3, 100_MB, 3, false, tc);
        CmdRead(0, 3, 1000, 1_KB, 2, false, tc);
    });
}

Y_UNIT_TEST(TestPQReadAhead) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;

        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {{"aaa", true}}, tc); //important client - never delete

        TVector<std::pair<ui64, TString>> data;

        ui32 pp = 8 + 4 + 2 + 9;
        TString tmp{1_MB - pp - 2, '-'};
        TString tmp0{32 - pp - 2, '-'};
        char k = 0;
        for (ui32 i = 0; i < 5; ++i) {
            TString ss = "";
            ss += k;
            ss += tmp0;
            ss += char((i + 1) % 256);
            ++k;
            data.push_back({i + 1, ss});
        }
        for (ui32 i = 0; i < 17_MB;) { //3 full blobs and 2 in head
            TString ss = "";
            ss += k;
            ss += tmp;
            ss += char((i + 10) % 256);
            ++k;
            data.push_back({i + 10, ss});
            i += ss.size() + pp;
        }
        CmdWrite(0, "sourceid0", data, tc, false, {}, true); //now 1 blob
        PQGetPartInfo(0, 22, tc);
        activeZone = true;
        CmdRead(0, 0, 1, 100_MB, 1, false, tc);
        CmdRead(0, 1, 1, 100_MB, 1, false, tc);
        CmdRead(0, 2, 1, 100_MB, 1, false, tc);
        CmdRead(0, 3, 1, 100_MB, 1, false, tc);
        CmdRead(0, 4, 10, 100_MB, 10, false, tc);
    });
}

Y_UNIT_TEST(TestOwnership) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);

        tc.Runtime->SetScheduledLimit(50);

        PQTabletPrepare(10, 100_MB, 0, {}, tc);

        TString cookie, cookie2;
        cookie = CmdSetOwner(0, tc).first;
        UNIT_ASSERT(!cookie.empty());
        cookie2 = CmdSetOwner(0, tc).first;
        UNIT_ASSERT(!cookie2.empty());
        UNIT_ASSERT(cookie2 != cookie);
    });
}

Y_UNIT_TEST(TestSetClientOffset) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        tc.Runtime->SetScheduledLimit(50);

        PQTabletPrepare(10, 100_MB, 0, {{"user1", false}}, tc);

        activeZone = true;

        TVector<std::pair<ui64, TString>> data;

        CmdSetOffset(0, "user1", 100, false, tc); //must be true , error
        CmdGetOffset(0, "user1", 0, tc); // must be -1

        activeZone = PlainOrSoSlow(true, false);

        CmdSetOffset(0, "user1", 0, false, tc);
        CmdGetOffset(0, "user1", 0, tc);
        CmdSetOffset(0, "user1", 0, false, tc);
        CmdGetOffset(0, "user1", 0, tc);
        CmdSetOffset(0, "user1", 0, false, tc);
        CmdGetOffset(0, "user1", 0, tc);
        CmdGetOffset(0, "user2", 0, tc);
    });
}

Y_UNIT_TEST(TestReadSessions) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        tc.Runtime->SetScheduledLimit(50);

        PQTabletPrepare(10, 100_MB, 0, {{"user1", false}}, tc);

        activeZone = true;

        TVector<std::pair<ui64, TString>> data;
        CmdCreateSession(0, "user1", "session1", tc);
        CmdSetOffset(0, "user1", 0, false, tc, "session1"); //all ok - session is set
        CmdSetOffset(0, "user1", 0, true, tc, "other_session"); //fails - session1 is active

        activeZone = PlainOrSoSlow(true, false);

        CmdSetOffset(0, "user1", 0, false, tc, "session1");

        CmdCreateSession(0, "user1", "session2", tc, 0, 1, 1);
        CmdCreateSession(0, "user1", "session3", tc, 0, 1, 1, true); //error on creation
        CmdCreateSession(0, "user1", "session3", tc, 0, 0, 2, true); //error on creation
        CmdCreateSession(0, "user1", "session3", tc, 0, 0, 0, true); //error on creation
        CmdSetOffset(0, "user1", 0, true, tc, "session1");
        CmdSetOffset(0, "user1", 0, true, tc, "session3");
        CmdSetOffset(0, "user1", 0, false, tc, "session2");

        activeZone = true;

        CmdKillSession(0, "user1", "session2", tc);
        CmdSetOffset(0, "user1", 0, true, tc, "session2"); //session is dead now
    });
}



Y_UNIT_TEST(TestGetTimestamps) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        tc.Runtime->SetScheduledLimit(50);

        tc.Runtime->UpdateCurrentTime(TInstant::Zero() + TDuration::Days(2));
        activeZone = false;

        PQTabletPrepare(10, 100_MB, 0, {{"user1", false}}, tc);

        TVector<std::pair<ui64, TString>> data;
        data.push_back({1, TString(1_KB, 'a')});
        data.push_back({2, TString(1_KB, 'a')});
        data.push_back({3, TString(1_KB, 'a')});
        data.push_back({4, TString(1_KB, 'a')});

        CmdWrite(0, "sourceid0", data, tc, false, {}, true, "", -1, 1);
        CmdGetOffset(0, "user1", 0, tc, -1);

        CmdSetOffset(0, "user1", 1, true, tc);
        CmdSetOffset(0, "user1", 0, true, tc);
        CmdGetOffset(0, "user1", 0, tc, Max<i64>());
        CmdSetOffset(0, "user1", 1, true, tc);
        CmdGetOffset(0, "user1", 1, tc, 1);
        CmdSetOffset(0, "user1", 3, true, tc);
        CmdGetOffset(0, "user1", 3, tc, 3);
        CmdSetOffset(0, "user1", 4, true, tc);
        CmdGetOffset(0, "user1", 4, tc, 4);
        CmdSetOffset(0, "user1", 5, true, tc);
        CmdGetOffset(0, "user1", 5, tc, 4);
        CmdSetOffset(0, "user1", 5, true, tc);
        CmdWrite(0, "sourceid1", data, tc, false, {}, false);
        CmdGetOffset(0, "user1", 5, tc, 5);
        RestartTablet(tc);
        CmdGetOffset(0, "user1", 5, tc, 5);

        CmdWrite(0, "sourceid2", data, tc, false, {}, false, "", -1,100);
        CmdRead(0, 100, Max<i32>(), Max<i32>(), 4, false, tc, {100,101,102,103}); //all offsets will be putted in cache

        //check offset inside gap
        CmdSetOffset(0, "user", 50, true, tc);
        CmdGetOffset(0, "user", 50, tc, 100);

        CmdSetOffset(0, "user", 101, true, tc);
        CmdGetOffset(0, "user", 101, tc, 101);
    });
}


Y_UNIT_TEST(TestChangeConfig) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        activeZone = false;
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(50);

        TVector<std::pair<ui64, TString>> data;

        ui32 pp = 8 + 4 + 2 + 9;
        TString tmp0{32 - pp - 2, '-'};
        char k = 0;
        for (ui32 i = 0; i < 5; ++i) {
            TString ss = "";
            ss += k;
            ss += tmp0;
            ss += char((i + 1) % 256);
            ++k;
            data.push_back({i + 1, ss});
        }

        PQTabletPrepare(100, 100_MB, 86400 * 2, {{"aaa", true}}, tc, 5);
        CmdWrite(0, "sourceid0", data, tc, false, {}, true); //now 1 blob
        PQTabletPrepare(5, 1_MB, 86400, {{"bbb", true}, {"ccc", true}}, tc, 10);
        data.pop_back(); //to be sure that after write partition will no be full
        CmdWrite(0, "sourceid1", data, tc, true); //partition is full
        CmdWrite(1, "sourceid2", data, tc);
        CmdWrite(9, "sourceid3", data, tc); //now 1 blob
    });
}

Y_UNIT_TEST(TestReadSubscription) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);
        activeZone = false;
        tc.Runtime->SetScheduledLimit(600);
        tc.Runtime->SetScheduledEventFilter(&tc.ImmediateLogFlushAndRequestTimeoutFilter);

        TVector<std::pair<ui64, TString>> data;

        ui32 pp = 8 + 4 + 2 + 9;
        TString tmp0{32 - pp - 2, '-'};
        char k = 0;
        for (ui32 i = 0; i < 5; ++i) {
            TString ss = "";
            ss += k;
            ss += tmp0;
            ss += char((i + 1) % 256);
            ++k;
            data.push_back({i + 1, ss});
        }

        PQTabletPrepare(100, 100_MB, 86400 * 2, {{"user1", true}}, tc, 5);
        CmdWrite(0, "sourceid0", data, tc, false, {}, true);

        TAutoPtr<IEventHandle> handle;
        TEvPersQueue::TEvResponse *result;
        THolder<TEvPersQueue::TEvRequest> request;

        request.Reset(new TEvPersQueue::TEvRequest);
        auto req = request->Record.MutablePartitionRequest();
        req->SetPartition(0);
        auto read = req->MutableCmdRead();
        read->SetOffset(5);
        read->SetClientId("user1");
        read->SetCount(5);
        read->SetBytes(1000000);
        read->SetTimeoutMs(5000);

        tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries());

        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle);

        UNIT_ASSERT(result);
        UNIT_ASSERT(result->Record.HasStatus());
        UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK); //read without write must be timeouted
        UNIT_ASSERT_EQUAL(result->Record.GetPartitionResponse().GetCmdReadResult().ResultSize(), 0); //read without write must be timeouted

        request.Reset(new TEvPersQueue::TEvRequest);
        req = request->Record.MutablePartitionRequest();
        req->SetPartition(0);
        read = req->MutableCmdRead();
        read->SetOffset(5);
        read->SetClientId("user1");
        read->SetCount(3);
        read->SetBytes(1000000);
        read->SetTimeoutMs(5000);

        tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries()); //got read

        CmdWrite(0, "sourceid1", data, tc); //write

        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle); //now got data

        UNIT_ASSERT(result);
        UNIT_ASSERT(result->Record.HasStatus());
        UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);
        UNIT_ASSERT_EQUAL(result->Record.GetPartitionResponse().GetCmdReadResult().ResultSize(), 3); //got response, but only for 3 from 5 writed blobs

        request.Reset(new TEvPersQueue::TEvRequest);
        req = request->Record.MutablePartitionRequest();
        req->SetPartition(0);
        read = req->MutableCmdRead();
        read->SetOffset(10);
        read->SetClientId("user1");
        read->SetCount(55);
        read->SetBytes(1000000);
        read->SetTimeoutMs(5000);

        tc.Runtime->SendToPipe(tc.TabletId, tc.Edge, request.Release(), 0, GetPipeConfigWithRetries()); //got read

        CmdWrite(0, "sourceid2", data, tc); //write

        result = tc.Runtime->GrabEdgeEvent<TEvPersQueue::TEvResponse>(handle); //now got data

        UNIT_ASSERT(result);
        UNIT_ASSERT(result->Record.HasStatus());
        UNIT_ASSERT_EQUAL(result->Record.GetErrorCode(), NPersQueue::NErrorCode::OK);
        UNIT_ASSERT_EQUAL(result->Record.GetPartitionResponse().GetCmdReadResult().ResultSize(), 5); //got response for whole written blobs
    });
}

//


Y_UNIT_TEST(TestPQCacheSizeManagement) {
    TTestContext tc;
    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);

        tc.Runtime->SetScheduledLimit(200);

        activeZone = false;
        PQTabletPrepare(20000000, 100_MB, 0, {{"aaa", true}}, tc); //important client - never delete

        TVector<std::pair<ui64, TString>> data;

        ui32 pp =  4 + 8 + 2 + 9 + 100;
        TString tmp{1_MB - pp - 2, '-'};
        char k = 0;
        for (ui32 i = 0; i < 26_MB;) {
            TString ss = "";
            ss += k;
            ss += tmp;
            ss += char((i + 1) % 256);
            ++k;
            data.push_back({i + 1, ss});
            i += ss.size() + pp;
        }
        CmdWrite(0, "sourceid0", data, tc, false, {}, true);
        PQGetPartInfo(0, 26, tc);

        TAutoPtr<IEventHandle> handle;
        for (ui32 i = 0; i < 10; ++i) {
            CmdRead(0, 0, 1, 100_MB, 1, false, tc);
            RestartTablet(tc);
        }
    });
}

Y_UNIT_TEST(TestOffsetEstimation) {
    std::deque<NPQ::TDataKey> container = {
        {NPQ::TKey(NPQ::TKeyPrefix::EType::TypeNone, 0, 1, 0, 0, 0), 0, TInstant::Seconds(1), 10},
        {NPQ::TKey(NPQ::TKeyPrefix::EType::TypeNone, 0, 2, 0, 0, 0), 0, TInstant::Seconds(1), 10},
        {NPQ::TKey(NPQ::TKeyPrefix::EType::TypeNone, 0, 3, 0, 0, 0), 0, TInstant::Seconds(2), 10},
        {NPQ::TKey(NPQ::TKeyPrefix::EType::TypeNone, 0, 4, 0, 0, 0), 0, TInstant::Seconds(2), 10},
        {NPQ::TKey(NPQ::TKeyPrefix::EType::TypeNone, 0, 5, 0, 0, 0), 0, TInstant::Seconds(3), 10},
        {NPQ::TKey(NPQ::TKeyPrefix::EType::TypeNone, 0, 6, 0, 0, 0), 0, TInstant::Seconds(3), 10},
    };
    UNIT_ASSERT_EQUAL(NPQ::GetOffsetEstimate({}, TInstant::MilliSeconds(0), 9999), 9999);
    UNIT_ASSERT_EQUAL(NPQ::GetOffsetEstimate(container, TInstant::MilliSeconds(0), 9999), 1);
    UNIT_ASSERT_EQUAL(NPQ::GetOffsetEstimate(container, TInstant::MilliSeconds(500), 9999), 1);
    UNIT_ASSERT_EQUAL(NPQ::GetOffsetEstimate(container, TInstant::MilliSeconds(1000), 9999), 1);
    UNIT_ASSERT_EQUAL(NPQ::GetOffsetEstimate(container, TInstant::MilliSeconds(1500), 9999), 3);
    UNIT_ASSERT_EQUAL(NPQ::GetOffsetEstimate(container, TInstant::MilliSeconds(2000), 9999), 3);
    UNIT_ASSERT_EQUAL(NPQ::GetOffsetEstimate(container, TInstant::MilliSeconds(2500), 9999), 5);
    UNIT_ASSERT_EQUAL(NPQ::GetOffsetEstimate(container, TInstant::MilliSeconds(3000), 9999), 5);
    UNIT_ASSERT_EQUAL(NPQ::GetOffsetEstimate(container, TInstant::MilliSeconds(3500), 9999), 9999);
}

Y_UNIT_TEST(TestMaxTimeLagRewind) {
    TTestContext tc;

    RunTestWithReboots(tc.TabletIds, [&]() {
        return tc.InitialEventsFilter.Prepare();
    }, [&](const TString& dispatchName, std::function<void(TTestActorRuntime&)> setup, bool& activeZone) {
        TFinalizer finalizer(tc);
        tc.Prepare(dispatchName, setup, activeZone);

        tc.Runtime->SetScheduledLimit(200);

        PQTabletPrepare(20000000, 100_MB, 0, {{"aaa", true}}, tc);
        activeZone = false;


        for (int i = 0; i < 5; i++) {
            TVector<std::pair<ui64, TString>> data;
            for (int j = 0; j < 7; j++) {
                data.push_back({7 * i + j + 1, TString(1_MB, 'a')});
            }
            CmdWrite(0, "sourceid0", data, tc, false, {}, i == 0);
            tc.Runtime->UpdateCurrentTime(tc.Runtime->GetCurrentTime() + TDuration::Minutes(1));
        }
        auto ts = tc.Runtime->GetCurrentTime();
        CmdRead(0, 0, 1, Max<i32>(), 1, false, tc, {0});
        CmdRead(0, 0, 1, Max<i32>(), 1, false, tc, {21}, 3 * 60 * 1000);
        CmdRead(0, 22, 1, Max<i32>(), 1, false, tc, {22}, 3 * 60 * 1000);
        CmdRead(0, 4, 1, Max<i32>(), 1, false, tc, {34}, 1000);

        CmdRead(0, 0, 1, Max<i32>(), 1, false, tc, {21}, 0, ts.MilliSeconds() - 3 * 60 * 1000);
        CmdRead(0, 22, 1, Max<i32>(), 1, false, tc, {22}, 0, ts.MilliSeconds() - 3 * 60 * 1000);
        CmdRead(0, 4, 1, Max<i32>(), 1, false, tc, {34}, 0, ts.MilliSeconds() - 1000);

        PQTabletPrepare(20000000, 100_MB, 0, {{"aaa", true}}, tc, 2, 6_MB, true, ts.MilliSeconds() - 1000);
        CmdRead(0, 0, 1, Max<i32>(), 1, false, tc, {34});

    });
}


Y_UNIT_TEST(TestWriteTimeStampEstimate) {
    TTestContext tc;
    TFinalizer finalizer(tc);
    tc.Prepare();

    tc.Runtime->SetScheduledLimit(150);
    tc.Runtime->SetDispatchTimeout(TDuration::Seconds(1));
    tc.Runtime->SetLogPriority(NKikimrServices::PERSQUEUE, NLog::PRI_DEBUG);

    PQTabletPrepare(20000000, 100_MB, 0, {{"aaa", true}}, tc);

    tc.Runtime->UpdateCurrentTime(TInstant::MilliSeconds(1000000));

    TVector<std::pair<ui64, TString>> data{{1,"abacaba"}};
    CmdWrite(0, "sourceid0", data, tc);

    CmdGetOffset(0, "user1", 0, tc, -1, 1000000);

    PQTabletPrepare(20000000, 100_MB, 0, {{"aaa", true}}, tc, 2, 6_MB, false);

    RestartTablet(tc);

    CmdGetOffset(0, "user1", 0, tc, -1, 0);

    tc.Runtime->UpdateCurrentTime(TInstant::MilliSeconds(2000000));

    data.front().first = 2;
    CmdWrite(0, "sourceid0", data, tc);

    CmdGetOffset(0, "user1", 0, tc, -1, 2000000);

    CmdUpdateWriteTimestamp(0, 3000000, tc);

    CmdGetOffset(0, "user1", 0, tc, -1, 3000000);

}



Y_UNIT_TEST(TestWriteTimeLag) {
    TTestContext tc;
    TFinalizer finalizer(tc);
    tc.Prepare();

    tc.Runtime->SetScheduledLimit(150);
    tc.Runtime->SetDispatchTimeout(TDuration::Seconds(1));
    tc.Runtime->SetLogPriority(NKikimrServices::PERSQUEUE, NLog::PRI_DEBUG);

    PQTabletPrepare(20000000, 1_TB, 0, {{"aaa", false}}, tc);

    TVector<std::pair<ui64, TString>> data{{1,TString(1024*1024, 'a')}};
    for (ui32 i = 0; i < 20; ++i) {
        CmdWrite(0, TStringBuilder() << "sourceid" << i, data, tc);
    }

    // After restart all caches are empty.
    RestartTablet(tc);

    PQTabletPrepare(20000000, 1_TB, 0, {{"aaa", false}, {"important", true}, {"another", true}}, tc);
    PQTabletPrepare(20000000, 1_TB, 0, {{"aaa", false}, {"another1", true}, {"important", true}}, tc);
    PQTabletPrepare(20000000, 1_TB, 0, {{"aaa", false}, {"another1", true}, {"important", true}, {"another", false}}, tc);

    CmdGetOffset(0, "important", 12, tc, -1, 0);

    CmdGetOffset(0, "another1", 12, tc, -1, 0);
    CmdGetOffset(0, "another", 0, tc, -1, 0);
    CmdGetOffset(0, "aaa", 0, tc, -1, 0);
}


void CheckEventSequence(TTestContext& tc, std::function<void()> scenario, std::deque<ui32> expectedEvents) {
    tc.Runtime->SetObserverFunc([&expectedEvents](TTestActorRuntimeBase&, TAutoPtr<IEventHandle>& ev) {
        if (!expectedEvents.empty() && ev->Type == expectedEvents.front()) {
            expectedEvents.pop_front();
        }
        return TTestActorRuntime::EEventAction::PROCESS;
    });

    TDispatchOptions options;
    options.CustomFinalCondition = [&expectedEvents](){
        return expectedEvents.empty();
    };
    options.FinalEvents.emplace_back(TEvPQ::EvEnd);  // dummy event to prevent early return from DispatchEvents

    scenario();

    UNIT_ASSERT(tc.Runtime->DispatchEvents(options));
    UNIT_ASSERT(expectedEvents.empty());
}

Y_UNIT_TEST(TestTabletRestoreEventsOrder) {
    TTestContext tc;
    TFinalizer finalizer(tc);
    tc.Prepare();

    // Scenario 1: expect EvTabletActive after empty tablet reboot
    CheckEventSequence(tc, /*scenario=*/[&tc]() {
        ForwardToTablet(*tc.Runtime, tc.TabletId, tc.Edge, new TEvents::TEvPoisonPill());
    }, /*expectedEvents=*/{
        TEvTablet::TEvRestored::EventType,
        TEvTablet::TEvTabletActive::EventType,
    });

    // Scenario 2: expect EvTabletActive only after partitions init complete
    CheckEventSequence(tc, /*scenario=*/[&tc]() {
        PQTabletPrepare(20000000, 100_MB, 0, {{"aaa", true}}, tc, /*partitions=*/2);
        ForwardToTablet(*tc.Runtime, tc.TabletId, tc.Edge, new TEvents::TEvPoisonPill());
    }, /*expectedEvents=*/{
        TEvTablet::TEvRestored::EventType,
        TEvPQ::TEvInitComplete::EventType,
        TEvPQ::TEvInitComplete::EventType,
        TEvTablet::TEvTabletActive::EventType,
    });
}

} // TPQTest
} // NKikimr
