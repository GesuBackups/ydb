# -*- coding: utf-8 -*-
import os
import ydb


def test_select_one():
    driver = ydb.Driver(ydb.DriverConfig(os.getenv('YDB_ENDPOINT'), os.getenv('YDB_DATABASE')))
    driver.wait()
    session = ydb.retry_operation_sync(lambda: driver.table_client.session().create())
    assert session.transaction().execute('select 1 as cnt;', commit_tx=True)[0].rows[0].cnt == 1

    session.execute_scheme("CREATE TABLE Data (Key Utf8, Value Uint32, PRIMARY KEY (Key));")

    rows = session.transaction().execute("SELECT * FROM Data WHERE Key LIKE 'foo%bar'", commit_tx=True)[0].rows
    assert len(rows) == 0


def test_create_table_with_default_profile():
    driver = ydb.Driver(ydb.DriverConfig(os.getenv('YDB_ENDPOINT'), os.getenv('YDB_DATABASE')))
    driver.wait()

    session = ydb.retry_operation_sync(lambda: driver.table_client.session().create())
    session.create_table(
        '/local/lz4',
        ydb.TableDescription()
        .with_primary_key('key')
        .with_columns(
            ydb.Column('key', ydb.OptionalType(ydb.PrimitiveType.Utf8)),
            ydb.Column('value', ydb.OptionalType(ydb.PrimitiveType.Utf8)),
        )
        .with_profile(
            ydb.TableProfile().with_preset_name('log_lz4')
        )
    )
