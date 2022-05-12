set host=%REMOTE_BUILD_HOST%
plink %host% "cd ~/arcadia/yql/udfs && python ~/arcadia/ya make"
plink %host% "rm ~/udfs/*"
plink %host% "find ~/arcadia/yql/udfs/ -name """"lib*.so"""" | xargs -I {} ln -s {} ~/udfs/"
if not exist ..\udfs_linux mkdir ..\udfs_linux
del /q ..\udfs_linux\*
pscp -C -r %host%:/home/%USERNAME%/udfs/* ..\udfs_linux\
