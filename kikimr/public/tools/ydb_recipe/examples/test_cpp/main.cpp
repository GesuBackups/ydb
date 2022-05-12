#include <util/system/env.h>
#include <library/cpp/testing/unittest/registar.h>

#include <ydb/public/sdk/cpp/client/ydb_driver/driver.h>
#include <ydb/public/sdk/cpp/client/ydb_table/table.h>

using namespace NYdb;
using namespace NYdb::NTable;

Y_UNIT_TEST_SUITE(Suite)
{
    Y_UNIT_TEST(Simple)
    {
        auto config = TDriverConfig()
            .SetEndpoint(GetEnv("YDB_ENDPOINT"))
            .SetDatabase(GetEnv("YDB_DATABASE"));
        auto driver = TDriver(config);
        auto tableClient = TTableClient(driver);
        auto session = tableClient.GetSession().GetValueSync().GetSession();

        auto result = session.ExecuteDataQuery(R"(
            Select 1;)", TTxControl::BeginTx(TTxSettings::SerializableRW()).CommitTx()).ExtractValueSync();

        UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
    }
}
