DEPENDS(
    kikimr/driver
    kikimr/public/tools/ydb_recipe/bin
)

USE_RECIPE(
    kikimr/public/tools/ydb_recipe/bin/ydb_recipe --suppress-version-check
)
