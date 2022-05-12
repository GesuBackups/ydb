# Recipe to setup Yandex Database (YDB) cluster in tests

If local YDB cluster is required in test, you can use this recipe to do so.
In such case, you have to put the line below in your ya.make file:
```
INCLUDE(${ARCADIA_ROOT}/kikimr/public/tools/ydb_recipe/recipe_stable.inc)
```
When this statement is included in your ya.make file, recipe will prepare local YDB cluster
in the single node mode. The `YDB_ENDPOINT` variable with cluster endpoint will be set in the environment.
Also `YDB_DATABASE`  variable with database name will be set in the environment. See `examples` directory
for additional details and usage examples.

Your test with this recipe can be executed as usual in the following way: ``ya make -tt``.

This recipe supports ``LINUX`` and ``DARWIN`` platforms. If you are using ``DARWIN`` platform, please make sure that ``/etc/hosts`` includes the following line ``127.0.0.1       <name>``, where
``<name>`` is a hostname of device you use. You can get hostname using the following line ``hostname -f``.

For additional details, please, contact ydb@yandex-team.ru.

