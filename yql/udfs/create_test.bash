#!/usr/bin/env bash 

set -e
# set -x

SCRIPTS_DIR="$(dirname $(readlink -f $0))"
REL_DIR=$1
ARC_REL_DIR="$(echo "${SCRIPTS_DIR}" | sed 's/.*\/arcadia\///')/$REL_DIR"
CMAKELISTS_TXT="$REL_DIR/ya.make"

cat >> $CMAKELISTS_TXT << EOF
EOF

TEST_DIR="$REL_DIR/test"
mkdir $TEST_DIR || true

TEST_CMAKELISTS_TXT="$TEST_DIR/ya.make"
cat >> $TEST_CMAKELISTS_TXT << EOF
OWNER(`whoami`)

YQL_UDF_TEST()

DEPENDS(
    $ARC_REL_DIR
)

END()
EOF

CASES_DIR="$TEST_DIR/cases"
mkdir $CASES_DIR || true

echo "Test created, to add cases:"
echo "  cd $TEST_DIR"
echo "  create cases/<TestName>.sql and cases/<TestName>.in"
echo "To run tests: ya make -t"
echo "To canonize tests output: ya make -tZ"
echo "After all commit $REL_DIR"
echo "Documentation: https://wiki.yandex-team.ru/yql/udf/#arkadijjnyeregressionnyeavtotestycherezyamake-t"
