DEPENDS(
    kikimr/public/tools/ydb_recipe
    kikimr/public/tools/package/stable
)

USE_RECIPE(
    kikimr/public/tools/ydb_recipe/ydb_recipe --use-packages kikimr/public/tools/package/stable
)
