# Public developers tools for Yandex Database (YDB)

**Main tools**
* `ydb` -- Command line tool for YDB (browse schema, execute queries, backup database and many more)

**Local YDB**
* `local_ydb` -- program to setup local YDB cluster in the single-node mode (for tests, debugging)
* `local_ydb_docker` -- build ydb docker image from trunk
* `local_ydb_stable_docker` -- build ydb docker image from stable branch

**Arcadia recipes for tests**
* `lbk_recipe` -- recipe for LogBroker
* `sqs_recipe` -- recipe for SQS, which is implementation of Amazon SQS API, also known as Cloud YMQ
* `ydb_recipe` -- recipe for YDB (usage examples are in ydb_recipe/examples)

**Other**
* `lib` -- libraries written in Python used by developer tools.
* `package` -- links to different stable/unstable packages
