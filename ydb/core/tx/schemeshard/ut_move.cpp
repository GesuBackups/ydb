#include <ydb/core/tx/schemeshard/ut_helpers/helpers.h>
#include <ydb/core/tx/schemeshard/schemeshard_utils.h>

#include <ydb/core/base/compile_time_flags.h>
#include <ydb/core/kqp/ut/common/kqp_ut_common.h>
#include <ydb/core/tx/datashard/change_exchange.h>

#include <util/generic/size_literals.h>
#include <util/string/cast.h>

using namespace NKikimr;
using namespace NSchemeShard;
using namespace NSchemeShardUT_Private;

Y_UNIT_TEST_SUITE(TSchemeShardMoveTest) {
    Y_UNIT_TEST(Boot) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
    }

    Y_UNIT_TEST(Reject) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime,
                     TTestEnvOptions()
                     .EnableAsyncIndexes(true));
        ui64 txId = 100;

        TestCreateIndexedTable(runtime, ++txId, "/MyRoot", R"(
            TableDescription {
              Name: "Table1"
              Columns { Name: "key"   Type: "Uint64" }
              Columns { Name: "value0" Type: "Utf8" }
              Columns { Name: "value1" Type: "Utf8" }
              KeyColumnNames: ["key"]
            }
            IndexDescription {
              Name: "Sync"
              KeyColumnNames: ["value0"]
            }
            IndexDescription {
              Name: "Async"
              KeyColumnNames: ["value1"]
              Type: EIndexTypeGlobalAsync
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestCreateIndexedTable(runtime, ++txId, "/MyRoot", R"(
            TableDescription {
              Name: "Table2"
              Columns { Name: "key"   Type: "Uint64" }
              Columns { Name: "value0" Type: "Utf8" }
              Columns { Name: "value1" Type: "Utf8" }
              KeyColumnNames: ["key"]
            }
            IndexDescription {
              Name: "Sync"
              KeyColumnNames: ["value0"]
            }
            IndexDescription {
              Name: "Async"
              KeyColumnNames: ["value1"]
              Type: EIndexTypeGlobalAsync
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(2),
                            NLs::PathsInsideDomain(10),
                            NLs::ShardsInsideDomain(6)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table1"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(3),
                            NLs::CheckColumns("Table1", {"key", "value0", "value1"}, {}, {"key"}),
                            NLs::IndexesCount(2)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table2"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(3),
                            NLs::CheckColumns("Table2", {"key", "value0", "value1"}, {}, {"key"}),
                            NLs::IndexesCount(2)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Moved1"),
                           {NLs::PathNotExist});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Moved2"),
                           {NLs::PathNotExist});

        {
            ++txId;
            auto op = MoveTableRequest(txId,  "/MyRoot/Table1", "/MyRoot/Table2");
            AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, op);
            TestModificationResult(runtime, txId, NKikimrScheme::StatusSchemeError);
        }

        {
            ++txId;
            auto op = MoveTableRequest(txId,  "/MyRoot/Table1", "/MyRoot/Table1");
            AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, op);
            TestModificationResult(runtime, txId, NKikimrScheme::StatusSchemeError);
        }

        {
            ++txId;
            auto first = MoveTableRequest(txId,  "/MyRoot/Table1", "/MyRoot/Moved1");
            auto second = MoveTableRequest(txId,  "/MyRoot/Table1", "/MyRoot/Moved2");
            auto combination = CombineSchemeTransactions({first, second});

            AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, combination);
            TestModificationResult(runtime, txId, NKikimrScheme::StatusInvalidParameter);
        }

        {
            ++txId;
            auto first = MoveTableRequest(txId,  "/MyRoot/Table1", "/MyRoot/Moved1");
            auto second = MoveTableRequest(txId,  "/MyRoot/Table2", "/MyRoot/Moved1");
            auto combination = CombineSchemeTransactions({first, second});

            AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, combination);
            TestModificationResult(runtime, txId, NKikimrScheme::StatusInvalidParameter);
        }

        {
            ++txId;
            auto first = DropTableRequest(txId,  "/MyRoot", "Table1");
            auto second = MoveTableRequest(txId,  "/MyRoot/Table1", "/MyRoot/Table1");
            auto combination = CombineSchemeTransactions({first, second});
            AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, combination);
            TestModificationResult(runtime, txId, NKikimrScheme::StatusInvalidParameter);
        }

        {
            ++txId;
            auto first = DropTableRequest(txId,  "/MyRoot", "Table2");
            auto second = MoveTableRequest(txId,  "/MyRoot/Table1", "/MyRoot/Moved1");
            auto third = MoveTableRequest(txId,  "/MyRoot/Table2", "/MyRoot/Moved1");
            auto combination = CombineSchemeTransactions({first, second, third});
            AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, combination);
            TestModificationResult(runtime, txId, NKikimrScheme::StatusInvalidParameter);
        }

        {
            ++txId;
            auto first = MoveTableRequest(txId,  "/MyRoot/Table1", "/MyRoot/Table2");
            auto second = MoveTableRequest(txId,  "/MyRoot/Table2", "/MyRoot/Table1");
            auto combination = CombineSchemeTransactions({first, second});
            AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, combination);
            TestModificationResult(runtime, txId, NKikimrScheme::StatusSchemeError);
        }

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(2),
                            NLs::PathsInsideDomain(10),
                            NLs::ShardsInsideDomain(6)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table1"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(3),
                            NLs::CheckColumns("Table1", {"key", "value0", "value1"}, {}, {"key"}),
                            NLs::IndexesCount(2)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table2"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(3),
                            NLs::CheckColumns("Table2", {"key", "value0", "value1"}, {}, {"key"}),
                            NLs::IndexesCount(2)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Moved1"),
                           {NLs::PathNotExist});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Moved2"),
                           {NLs::PathNotExist});

        {
            //seccess op
            ++txId;
            auto first = MoveTableRequest(txId,  "/MyRoot/Table2", "/MyRoot/Moved2");
            auto second = MoveTableRequest(txId,  "/MyRoot/Table1", "/MyRoot/Table2");
            auto combination = CombineSchemeTransactions({first, second});
            AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, combination);
            TestModificationResult(runtime, txId);
            env.TestWaitNotification(runtime, txId);

            TestDescribeResult(DescribePath(runtime, "/MyRoot/Table1"),
                               {NLs::PathNotExist});

            TestDescribeResult(DescribePath(runtime, "/MyRoot/Table2"),
                               {NLs::IsTable,
                                NLs::PathVersionEqual(6),
                                NLs::CheckColumns("Table2", {"key", "value0", "value1"}, {}, {"key"})});

            TestDescribeResult(DescribePath(runtime, "/MyRoot/Moved2"),
                               {NLs::IsTable,
                                NLs::PathVersionEqual(6),
                                NLs::CheckColumns("Moved2", {"key", "value0", "value1"}, {}, {"key"})});

            TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                               {NLs::ChildrenCount(2),
                                NLs::PathsInsideDomain(10),
                                NLs::ShardsInsideDomain(6)});
        }
    }

    Y_UNIT_TEST(MoveTableForBackup) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime,
                     TTestEnvOptions());

        ui64 txId = 100;

        // create src table
        TestCreateTable(runtime, ++txId, "/MyRoot", R"(
            Name: "Table"
            Columns { Name: "key" Type: "Uint32"}
            Columns { Name: "value" Type: "Utf8"}
            KeyColumnNames: ["key"]
        )");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table", true), {
            NLs::IsBackupTable(false),
        });

        // simple copy table
        TestCreateTable(runtime, ++txId, "/MyRoot", R"(
            Name: "IsBackupTable"
            CopyFromTable: "/MyRoot/Table"
            IsBackup: true
        )");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot/IsBackupTable", true), {
            NLs::IsBackupTable(true),
        });

        {
            ++txId;
            auto op = MoveTableRequest(txId, "/MyRoot/IsBackupTable","/MyRoot/IsBackupTableMoved");
            AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, op);
            TestModificationResult(runtime, txId, NKikimrScheme::StatusSchemeError);
        }

        TestDescribeResult(DescribePath(runtime, "/MyRoot/IsBackupTableMoved", true), {
             NLs::PathNotExist
        });

        TestDescribeResult(DescribePath(runtime, "/MyRoot/IsBackupTable", true), {
            NLs::IsBackupTable(true),
        });

        TestDropTable(runtime, ++txId, "/MyRoot", "IsBackupTable");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot/IsBackupTable", true), {
             NLs::PathNotExist
        });
    }


    Y_UNIT_TEST(TwoTables) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime,
                     TTestEnvOptions());
        ui64 txId = 100;

        TestCreateTable(runtime, ++txId, "/MyRoot", R"(
              Name: "Table1"
              Columns { Name: "key"   Type: "Uint64" }
              Columns { Name: "value" Type: "Utf8" }
              KeyColumnNames: ["key"]
        )");
        env.TestWaitNotification(runtime, txId);

        TestCreateTable(runtime, ++txId, "/MyRoot", R"(
              Name: "Table2"
              Columns { Name: "key"   Type: "Uint64" }
              Columns { Name: "value" Type: "Utf8" }
              KeyColumnNames: ["key"]
        )");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(2),
                            NLs::PathsInsideDomain(2),
                            NLs::ShardsInsideDomain(2)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table1"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(3),
                            NLs::CheckColumns("Table1", {"key", "value"}, {}, {"key"})});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table2"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(3),
                            NLs::CheckColumns("Table2", {"key", "value"}, {}, {"key"})});

        ++txId;
        auto first = MoveTableRequest(txId,  "/MyRoot/Table1", "/MyRoot/TableMove1");
        auto second = MoveTableRequest(txId,  "/MyRoot/Table2", "/MyRoot/TableMove2");
        auto combination = CombineSchemeTransactions({first, second});
        AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, combination);
        TestModificationResult(runtime, txId);
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table1"),
                           {NLs::PathNotExist});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/TableMove1"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(6),
                            NLs::CheckColumns("TableMove1", {"key", "value"}, {}, {"key"})});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table2"),
                           {NLs::PathNotExist});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/TableMove2"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(6),
                            NLs::CheckColumns("TableMove2", {"key", "value"}, {}, {"key"})});

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(2),
                            NLs::PathsInsideDomain(2),
                            NLs::ShardsInsideDomain(2)});
    }

    Y_UNIT_TEST(Replace) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime,
                     TTestEnvOptions()
                     .EnableAsyncIndexes(true));
        ui64 txId = 100;

        TestCreateIndexedTable(runtime, ++txId, "/MyRoot", R"(
            TableDescription {
              Name: "Src"
              Columns { Name: "key"   Type: "Uint64" }
              Columns { Name: "value0" Type: "Utf8" }
              Columns { Name: "value1" Type: "Utf8" }
              KeyColumnNames: ["key"]
            }
            IndexDescription {
              Name: "Sync"
              KeyColumnNames: ["value0"]
            }
            IndexDescription {
              Name: "Async"
              KeyColumnNames: ["value1"]
              Type: EIndexTypeGlobalAsync
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestCreateIndexedTable(runtime, ++txId, "/MyRoot", R"(
            TableDescription {
              Name: "Dst"
              Columns { Name: "key"   Type: "Uint64" }
              Columns { Name: "value0" Type: "Utf8" }
              Columns { Name: "value1" Type: "Utf8" }
              KeyColumnNames: ["key"]
            }
            IndexDescription {
              Name: "Sync"
              KeyColumnNames: ["value0"]
            }
            IndexDescription {
              Name: "Async"
              KeyColumnNames: ["value1"]
              Type: EIndexTypeGlobalAsync
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(2),
                            NLs::PathsInsideDomain(10),
                            NLs::ShardsInsideDomain(6)});

        {
            ++txId;
            auto first = DropTableRequest(txId,  "/MyRoot", "Dst");
            auto second = MoveTableRequest(txId,  "/MyRoot/Src", "/MyRoot/Dst");
            auto combination = CombineSchemeTransactions({first, second});
            AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, combination);
            TestModificationResult(runtime, txId);
            env.TestWaitNotification(runtime, txId);
        }

        env.TestWaitTabletDeletion(runtime, xrange(TTestTxConfig::FakeHiveTablets+3, TTestTxConfig::FakeHiveTablets+6));


        TestDescribeResult(DescribePath(runtime, "/MyRoot/Src"),
                           {NLs::PathNotExist});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Dst"),
                           {NLs::IsTable,
                            NLs::PathIdEqual(12),
                            NLs::PathVersionEqual(6),
                            NLs::CheckColumns("Dst", {"key", "value0", "value1"}, {}, {"key"}),
                            NLs::IndexesCount(2)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(1),
                            NLs::PathsInsideDomain(5),
                            NLs::ShardsInsideDomain(3)});

        TestCreateIndexedTable(runtime, ++txId, "/MyRoot", R"(
            TableDescription {
              Name: "Src"
              Columns { Name: "key"   Type: "Uint64" }
              Columns { Name: "value0" Type: "Utf8" }
              Columns { Name: "value1" Type: "Utf8" }
              KeyColumnNames: ["key"]
            }
            IndexDescription {
              Name: "Sync"
              KeyColumnNames: ["value0"]
            }
            IndexDescription {
              Name: "Async"
              KeyColumnNames: ["value1"]
              Type: EIndexTypeGlobalAsync
            }
        )");
        env.TestWaitNotification(runtime, txId);

        {
            ++txId;
            auto first = DropTableRequest(txId,  "/MyRoot", "Dst");
            auto second = MoveTableRequest(txId,  "/MyRoot/Src", "/MyRoot/Dst");
            auto combination = CombineSchemeTransactions({first, second});
            AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, combination);
            TestModificationResult(runtime, txId);
            env.TestWaitNotification(runtime, txId);
        }

        env.TestWaitTabletDeletion(runtime, xrange(TTestTxConfig::FakeHiveTablets, TTestTxConfig::FakeHiveTablets+3));

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Src"),
                           {NLs::PathNotExist});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Dst"),
                           {NLs::IsTable,
                            NLs::PathIdEqual(22),
                            NLs::PathVersionEqual(6),
                            NLs::CheckColumns("Dst", {"key", "value0", "value1"}, {}, {"key"}),
                            NLs::IndexesCount(2)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(1),
                            NLs::PathsInsideDomain(5),
                            NLs::ShardsInsideDomain(3)});
    }

    Y_UNIT_TEST(Chain) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime,
                     TTestEnvOptions()
                     .EnableAsyncIndexes(true));
        ui64 txId = 100;

        TestCreateIndexedTable(runtime, ++txId, "/MyRoot", R"(
            TableDescription {
              Name: "table1"
              Columns { Name: "key"   Type: "Uint64" }
              Columns { Name: "value0" Type: "Utf8" }
              Columns { Name: "value1" Type: "Utf8" }
              KeyColumnNames: ["key"]
            }
            IndexDescription {
              Name: "Sync"
              KeyColumnNames: ["value0"]
            }
            IndexDescription {
              Name: "Async"
              KeyColumnNames: ["value1"]
              Type: EIndexTypeGlobalAsync
            }

        )");
        env.TestWaitNotification(runtime, txId);

        TestCreateIndexedTable(runtime, ++txId, "/MyRoot", R"(
            TableDescription {
              Name: "table2"
              Columns { Name: "key"   Type: "Uint64" }
              Columns { Name: "value0" Type: "Utf8" }
              Columns { Name: "value1" Type: "Utf8" }
              KeyColumnNames: ["key"]
            }
            IndexDescription {
              Name: "Sync"
              KeyColumnNames: ["value0"]
            }
            IndexDescription {
              Name: "Async"
              KeyColumnNames: ["value1"]
              Type: EIndexTypeGlobalAsync
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(2),
                            NLs::PathsInsideDomain(10),
                            NLs::ShardsInsideDomain(6)});

        ++txId;
        auto first = MoveTableRequest(txId,  "/MyRoot/table2", "/MyRoot/table3");
        auto second = MoveTableRequest(txId,  "/MyRoot/table1", "/MyRoot/table2");
        auto combination = CombineSchemeTransactions({first, second});
        AsyncSendTransaction(runtime, TTestTxConfig::SchemeShard, combination);
        TestModificationResult(runtime, txId);
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot/table1"),
                           {NLs::PathNotExist});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/table2"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(6),
                            NLs::CheckColumns("table2", {"key", "value0", "value1"}, {}, {"key"})});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/table3"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(6),
                            NLs::CheckColumns("table3", {"key", "value0", "value1"}, {}, {"key"})});

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(2),
                            NLs::PathsInsideDomain(10),
                            NLs::ShardsInsideDomain(6)});
    }

    Y_UNIT_TEST(OneTable) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime,
                     TTestEnvOptions()
                     .EnableAsyncIndexes(true));
        ui64 txId = 100;

        TestCreateTable(runtime, ++txId, "/MyRoot", R"(
              Name: "Table"
              Columns { Name: "key"   Type: "Uint64" }
              Columns { Name: "value" Type: "Utf8" }
              KeyColumnNames: ["key"]
        )");

        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(1),
                            NLs::PathsInsideDomain(1),
                            NLs::ShardsInsideDomain(1)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(3),
                            NLs::CheckColumns("Table", {"key", "value"}, {}, {"key"}),
                            NLs::PathsInsideDomain(1),
                            NLs::ShardsInsideDomain(1)});


        TestMoveTable(runtime, ++txId, "/MyRoot/Table", "/MyRoot/TableMove");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table"),
                           {NLs::PathNotExist});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/TableMove"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(6),
                            NLs::CheckColumns("TableMove", {"key", "value"}, {}, {"key"}),
                            NLs::PathsInsideDomain(1),
                            NLs::ShardsInsideDomain(1)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(1),
                            NLs::PathsInsideDomain(1),
                            NLs::ShardsInsideDomain(1)});

        TestAlterTable(runtime, ++txId, "/MyRoot",
                R"(Name: "TableMove" Columns { Name: "add" Type: "Utf8" })");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot/TableMove"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(7),
                            NLs::CheckColumns("TableMove", {"key", "value", "add"}, {}, {"key"}),
                            NLs::PathsInsideDomain(1),
                            NLs::ShardsInsideDomain(1)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(1),
                            NLs::PathsInsideDomain(1),
                            NLs::ShardsInsideDomain(1)});

        TestMoveTable(runtime, ++txId, "/MyRoot/TableMove", "/MyRoot/TableMoveTwice");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot/TableMoveTwice"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(10),
                            NLs::CheckColumns("TableMoveTwice", {"key", "value", "add"}, {}, {"key"}),
                            NLs::PathsInsideDomain(1),
                            NLs::ShardsInsideDomain(1)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(1),
                            NLs::PathsInsideDomain(1),
                            NLs::ShardsInsideDomain(1)});

        TestCopyTable(runtime, ++txId, "/MyRoot", "TableCopy", "/MyRoot/TableMoveTwice");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot/TableCopy"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(3),
                            NLs::CheckColumns("TableCopy", {"key", "value", "add"}, {}, {"key"}),
                            NLs::PathsInsideDomain(2),
                            NLs::ShardsInsideDomain(2)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(2),
                            NLs::PathsInsideDomain(2),
                            NLs::ShardsInsideDomain(2)});

        TestMoveTable(runtime, ++txId, "/MyRoot/TableCopy", "/MyRoot/TableCopyMove");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot/TableCopyMove"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(6),
                            NLs::CheckColumns("TableCopyMove", {"key", "value", "add"}, {}, {"key"}),
                            NLs::PathsInsideDomain(2),
                            NLs::ShardsInsideDomain(2)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(2),
                            NLs::PathsInsideDomain(2),
                            NLs::ShardsInsideDomain(2)});

        TestDropTable(runtime, ++txId, "/MyRoot", "TableCopyMove");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(1),
                            NLs::PathsInsideDomain(1),
                            NLs::ShardsInsideDomainOneOf({1, 2})});

        TestDropTable(runtime, ++txId, "/MyRoot", "TableMoveTwice");
        env.TestWaitNotification(runtime, txId);

        env.TestWaitTabletDeletion(runtime, {9437194, 9437195});

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(0),
                            NLs::PathsInsideDomain(0),
                            NLs::ShardsInsideDomain(0)});
    }

    Y_UNIT_TEST(Index) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime,
                     TTestEnvOptions()
                     .EnableAsyncIndexes(true));
        ui64 txId = 100;

        TestCreateIndexedTable(runtime, ++txId, "/MyRoot", R"(
            TableDescription {
              Name: "Table"
              Columns { Name: "key"   Type: "Uint64" }
              Columns { Name: "value0" Type: "Utf8" }
              Columns { Name: "value1" Type: "Utf8" }
              KeyColumnNames: ["key"]
            }
            IndexDescription {
              Name: "Sync"
              KeyColumnNames: ["value0"]
            }
            IndexDescription {
              Name: "Async"
              KeyColumnNames: ["value1"]
              Type: EIndexTypeGlobalAsync
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(1),
                            NLs::PathsInsideDomain(5),
                            NLs::ShardsInsideDomain(3)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(3),
                            NLs::CheckColumns("Table", {"key", "value0", "value1", "valueFloat"}, {}, {"key"}),
                            NLs::IndexesCount(2)});

        TestMoveTable(runtime, ++txId, "/MyRoot/Table", "/MyRoot/TableMove");
        env.TestWaitNotification(runtime, txId);

        TestDescribeResult(DescribePath(runtime, "/MyRoot/Table"),
                           {NLs::PathNotExist});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/TableMove"),
                           {NLs::IsTable,
                            NLs::PathVersionEqual(6),
                            NLs::CheckColumns("TableMove", {"key", "value0", "value1", "valueFloat"}, {}, {"key"})});

        TestDescribeResult(DescribePath(runtime, "/MyRoot"),
                           {NLs::ChildrenCount(1),
                            NLs::PathsInsideDomain(5),
                            NLs::ShardsInsideDomain(3)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/TableMove/Sync", true, true, true),
                           {NLs::PathExist,
                            NLs::IndexType(NKikimrSchemeOp::EIndexTypeGlobal),
                            NLs::IndexKeys({"value0"}),
                            NLs::IndexState(NKikimrSchemeOp::EIndexState::EIndexStateReady)});

        TestDescribeResult(DescribePath(runtime, "/MyRoot/TableMove/Async", true, true, true),
                           {NLs::PathExist,
                            NLs::IndexType(NKikimrSchemeOp::EIndexTypeGlobalAsync),
                            NLs::IndexKeys({"value1"}),
                            NLs::IndexState(NKikimrSchemeOp::EIndexState::EIndexStateReady)});
    }

    Y_UNIT_TEST(AsyncIndexWithSyncInFly) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime,
                     TTestEnvOptions()
                     .EnableAsyncIndexes(true));
        ui64 txId = 100;

        TestCreateIndexedTable(runtime, ++txId, "/MyRoot", R"(
            TableDescription {
              Name: "Table"
              Columns { Name: "key" Type: "Uint64" }
              Columns { Name: "indexed" Type: "Uint64" }
              KeyColumnNames: ["key"]
            }
            IndexDescription {
              Name: "UserDefinedIndex"
              KeyColumnNames: ["indexed"]
              Type: EIndexTypeGlobalAsync
            }
        )");
        env.TestWaitNotification(runtime, txId);

        using namespace NKikimr::NMiniKQL;

        bool NoActiveZone = false;
        TFakeDataReq req1(runtime, ++txId, "/MyRoot/Table",
                            R"(
                            (
                                (let row1 '( '('key (Uint64 '1)) ))
                                (let myUpd '( '('indexed (Uint64 '111)) ))
                                (let ret (AsList
                                    (UpdateRow '/MyRoot/Table row1 myUpd)
                                ))
                                (return ret)
                            )
                            )");
        IEngineFlat::EStatus status1 = req1.Propose(false, NoActiveZone);
        UNIT_ASSERT_VALUES_EQUAL_C(status1, IEngineFlat::EStatus::Unknown, "This Tx should be accepted and wait for Plan");
        UNIT_ASSERT(req1.GetErrors().empty());

        {
            TVector<THolder<IEventHandle>> supressed;
            auto defOberver = SetSuppressObserver(runtime, supressed, NDataShard::TEvChangeExchange::EvApplyRecords);

            req1.Plan(TTestTxConfig::Coordinator);

            WaitForSuppressed(runtime, supressed, 1, defOberver);
            UNIT_ASSERT(supressed.size() == 1);
        }

        {
            AsyncMoveTable(runtime, ++txId, "/MyRoot/Table", "/MyRoot/TableMove");

            TDispatchOptions opts;
            opts.FinalEvents.emplace_back(TDispatchOptions::TFinalEventCondition(NDataShard::TEvChangeExchange::EvStatus, 2));
            runtime.DispatchEvents(opts);

            env.TestWaitNotification(runtime, txId);
        }

        // Check result
        {
            NKikimrMiniKQL::TResult result;
            TString err;
            ui32 status = LocalMiniKQL(runtime, TTestTxConfig::FakeHiveTablets, R"(
            (
                (let range '( '('indexed (Uint64 '0) (Void) )  '('key (Uint64 '0) (Void) )))
                (let columns '('key 'indexed) )
                (let result (SelectRange '__user__indexImplTable range columns '()))
                (return (AsList (SetResult 'Result result) ))
            )
            )", result, err);

            UNIT_ASSERT_VALUES_EQUAL_C(static_cast<NKikimrProto::EReplyStatus>(status), NKikimrProto::OK, err);
            UNIT_ASSERT_VALUES_EQUAL(err, "");

            NKqp::CompareYson(R"([[[[[["111"];["1"]]];%false]]])", result);
        }
    }
}
