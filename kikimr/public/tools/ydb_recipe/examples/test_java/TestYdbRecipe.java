import org.junit.Test;
import org.junit.Assert;

import java.util.function.Consumer;
import java.util.Arrays;

import com.yandex.ydb.core.Result;
import com.yandex.ydb.core.Status;
import com.yandex.ydb.core.grpc.GrpcTransport;
import com.yandex.ydb.table.TableClient;
import com.yandex.ydb.table.transaction.TxControl;
import com.yandex.ydb.table.query.DataQueryResult;
import com.yandex.ydb.table.Session;
import com.yandex.ydb.table.rpc.grpc.GrpcTableRpc;
import com.yandex.ydb.table.result.ResultSetReader;
import com.yandex.ydb.table.settings.ExecuteScanQuerySettings;
import com.yandex.ydb.table.query.Params;

import static com.yandex.ydb.table.YdbTable.ExecuteScanQueryRequest.Mode.MODE_EXEC;

public class TestYdbRecipe {
    @Test
    public void test() {
        String ydbDatabase = System.getenv("YDB_DATABASE");
        String ydbEndpoint = System.getenv("YDB_ENDPOINT");
        Assert.assertTrue(ydbDatabase != null && !ydbDatabase.isEmpty());
        Assert.assertTrue(ydbEndpoint != null && !ydbEndpoint.isEmpty());

        GrpcTransport.Builder transportBuilder = GrpcTransport.forEndpoint(ydbEndpoint, ydbDatabase);
        GrpcTransport transport = transportBuilder.build();

        TableClient tableClient = TableClient.newClient(GrpcTableRpc.useTransport(transport))
            .build();

        Session session = tableClient.createSession()
            .join()
            .expect("cannot create session");

        TxControl txControl = TxControl.serializableRw().setCommitTx(true);
        Result<DataQueryResult> result = session.executeDataQuery("select 1;", txControl)
            .join();

        Assert.assertTrue(result.isSuccess());
        DataQueryResult dqresult = result
            .expect("cannot get result");

        ResultSetReader resultSet = dqresult.getResultSet(0);
        Assert.assertTrue(resultSet.next());
        long one = resultSet.getColumn(0).getInt32();
        Assert.assertTrue(Integer.valueOf(1) == one);

        {
            String query = String.format("select 42;");
            ExecuteScanQuerySettings settings = ExecuteScanQuerySettings.newBuilder()
                // .mode(MODE_EXEC)
                .build();
            Params params = Params.create();
            Status sqStatus = session.executeScanQuery(query, params, settings, rs -> {}).join();

            System.out.println(String.format("status -> %s", Arrays.toString(sqStatus.getIssues())));
            if (!sqStatus.isSuccess()) {
                throw new IllegalStateException("ScanQuery failed");
            }
        }

    }
}
