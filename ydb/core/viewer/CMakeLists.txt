
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(ydb-core-viewer)
target_compile_options(ydb-core-viewer PRIVATE
  -DUSE_CURRENT_UDF_ABI_VERSION
)
target_link_libraries(ydb-core-viewer PUBLIC
  contrib-libs-cxxsupp
  yutil
  library-cpp-resource
  cpp-actors-core
  library-cpp-archive
  cpp-mime-types
  ydb-core-base
  core-blobstorage-base
  core-client-server
  ydb-core-health_check
  ydb-core-node_whiteboard
  ydb-core-protos
  ydb-core-scheme
  core-tx-schemeshard
  ydb-core-util
  core-viewer-json
  core-viewer-protos
  library-persqueue-topic_parser
  api-protos
  lib-deprecated-kicli
)
target_sources(ydb-core-viewer PRIVATE
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/viewer.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/wb_aggregate.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/wb_filter.cpp
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/wb_merge.cpp
)

add_global_library_for(ydb-core-viewer.global ydb-core-viewer)
target_compile_options(ydb-core-viewer.global PRIVATE
  -DUSE_CURRENT_UDF_ABI_VERSION
)
target_link_libraries(ydb-core-viewer.global PUBLIC
  contrib-libs-cxxsupp
  yutil
  library-cpp-resource
  cpp-actors-core
  library-cpp-archive
  cpp-mime-types
  ydb-core-base
  core-blobstorage-base
  core-client-server
  ydb-core-health_check
  ydb-core-node_whiteboard
  ydb-core-protos
  ydb-core-scheme
  core-tx-schemeshard
  ydb-core-util
  core-viewer-json
  core-viewer-protos
  library-persqueue-topic_parser
  api-protos
  lib-deprecated-kicli
)
target_sources(ydb-core-viewer.global PRIVATE
  ${CMAKE_BINARY_DIR}/ydb/core/viewer/f083176314ed002a1f504f746fc2553b.cpp
)
resources(ydb-core-viewer.global
  ${CMAKE_BINARY_DIR}/ydb/core/viewer/f083176314ed002a1f504f746fc2553b.cpp
  INPUTS
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/index.html
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/23.c91c8ccf.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/25.e1e7d81a.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/61.cf8d37dc.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/43.2f0fa127.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/64.5456c2c7.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/42.c8452d83.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/28.889b6611.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/56.094415aa.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/8.4249de62.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/6.1c300a53.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/37.8bd311d5.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/11.f00dc151.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/27.a882e6d4.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/70.3f29d08a.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/35.2155030c.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/38.2523e6e8.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/22.4fcb77c5.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/9.7ad374a4.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/12.0a6b3185.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/52.fa68fd59.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/39.94b515ee.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/47.5c5fbd32.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/10.1663f149.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/24.086f1c96.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/1.00298cdb.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/runtime-main.1f4bf331.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/15.d7cfc9c7.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/26.ab136b87.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/76.9e0fcd6c.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/29.f207d61c.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/18.79da197d.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/46.d46c9132.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/0.46ee130e.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/65.b1ceabf6.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/31.5103f298.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/55.f9447ebb.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/20.31e12d9b.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/71.fc834a7c.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/30.d27a0016.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/75.0242ef21.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/49.c7f95d00.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/5.1cb3dd5e.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/40.0c9ddbc1.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/main.5a10f92f.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/57.b3628e0e.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/63.76168228.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/62.a5e87602.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/69.d354c945.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/33.f874acc9.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/54.0d770520.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/32.a4bcafec.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/44.a5b7267e.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/51.1eb8cb89.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/7.34d8b7fb.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/41.4c87b95e.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/77.8396f8e8.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/59.1e98d62c.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/78.6d95ceab.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/66.a76b11fa.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/68.82a7b8f6.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/36.810c51c0.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/60.69ebed0d.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/21.28e040b8.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/16.829d825a.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/14.232b4b6e.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/17.1314dfa0.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/50.65300557.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/13.9514e098.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/73.b8e3f76c.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/34.9ac6c889.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/74.048824c9.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/45.e46d1e81.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/48.fd25e54e.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/72.7f46887b.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/58.9be33e32.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/53.6022c001.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/19.a00df727.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/67.7b9bff8b.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/js/4.7189f7fd.chunk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/css/main.c9c40502.chunk.css
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/css/4.3e61f2b6.chunk.css
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/media/codicon.80a4c25b.ttf
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/monitoring/resources/favicon.png
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/index.html
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/viewer.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/jstree.min.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/style.min.css
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/throbber.gif
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/32px.png
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/40px.png
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/cpu
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/cpu_view.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/disk_cell.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/disk_map.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/index.html
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/man-green.png
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/man-orange.png
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/man-red.png
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/man-yellow.png
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/net_view.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/network
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/node_group.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/node.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/node_map.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/nodes
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/node_view.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/overview
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/overview.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/pdisk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/pool_block.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/pool_map.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/runner.html
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/stats.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/storage
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/storage_group.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/storage.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/storage_view.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/tablet_cell.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/tablet_map.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/tenant.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/tenants
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/tenant_view.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/throbber.gif
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/util.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/vdisk.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/viewer.css
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/v2/viewer.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/css/print.css
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/css/reset.css
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/css/screen.css
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/css/style.css
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/css/typography.css
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/fonts/DroidSans-Bold.ttf
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/fonts/DroidSans.ttf
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/images/collapse.gif
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/images/expand.gif
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/images/explorer_icons.png
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/images/favicon-16x16.png
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/images/favicon-32x32.png
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/images/favicon.ico
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/images/logo_small.png
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/images/throbber.gif
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/index.html
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/ca.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/en.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/es.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/fr.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/geo.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/it.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/ja.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/ko-kr.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/pl.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/pt.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/ru.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/tr.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/translator.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lang/zh-cn.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/backbone-min.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/es5-shim.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/handlebars-4.0.5.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/highlight.9.1.0.pack.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/highlight.9.1.0.pack_extended.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/jquery-1.8.0.min.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/jquery.ba-bbq.min.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/jquery.slideto.min.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/jquery.wiggle.min.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/js-yaml.min.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/jsoneditor.min.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/lodash.min.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/marked.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/object-assign-pollyfill.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/sanitize-html.min.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/lib/swagger-oauth.js
  ${CMAKE_SOURCE_DIR}/ydb/core/viewer/content/api/swagger-ui.min.js
  KEYS
  monitoring/index.html
  monitoring/resources/js/23.c91c8ccf.chunk.js
  monitoring/resources/js/25.e1e7d81a.chunk.js
  monitoring/resources/js/61.cf8d37dc.chunk.js
  monitoring/resources/js/43.2f0fa127.chunk.js
  monitoring/resources/js/64.5456c2c7.chunk.js
  monitoring/resources/js/42.c8452d83.chunk.js
  monitoring/resources/js/28.889b6611.chunk.js
  monitoring/resources/js/56.094415aa.chunk.js
  monitoring/resources/js/8.4249de62.chunk.js
  monitoring/resources/js/6.1c300a53.chunk.js
  monitoring/resources/js/37.8bd311d5.chunk.js
  monitoring/resources/js/11.f00dc151.chunk.js
  monitoring/resources/js/27.a882e6d4.chunk.js
  monitoring/resources/js/70.3f29d08a.chunk.js
  monitoring/resources/js/35.2155030c.chunk.js
  monitoring/resources/js/38.2523e6e8.chunk.js
  monitoring/resources/js/22.4fcb77c5.chunk.js
  monitoring/resources/js/9.7ad374a4.chunk.js
  monitoring/resources/js/12.0a6b3185.chunk.js
  monitoring/resources/js/52.fa68fd59.chunk.js
  monitoring/resources/js/39.94b515ee.chunk.js
  monitoring/resources/js/47.5c5fbd32.chunk.js
  monitoring/resources/js/10.1663f149.chunk.js
  monitoring/resources/js/24.086f1c96.chunk.js
  monitoring/resources/js/1.00298cdb.chunk.js
  monitoring/resources/js/runtime-main.1f4bf331.js
  monitoring/resources/js/15.d7cfc9c7.chunk.js
  monitoring/resources/js/26.ab136b87.chunk.js
  monitoring/resources/js/76.9e0fcd6c.chunk.js
  monitoring/resources/js/29.f207d61c.chunk.js
  monitoring/resources/js/18.79da197d.chunk.js
  monitoring/resources/js/46.d46c9132.chunk.js
  monitoring/resources/js/0.46ee130e.chunk.js
  monitoring/resources/js/65.b1ceabf6.chunk.js
  monitoring/resources/js/31.5103f298.chunk.js
  monitoring/resources/js/55.f9447ebb.chunk.js
  monitoring/resources/js/20.31e12d9b.chunk.js
  monitoring/resources/js/71.fc834a7c.chunk.js
  monitoring/resources/js/30.d27a0016.chunk.js
  monitoring/resources/js/75.0242ef21.chunk.js
  monitoring/resources/js/49.c7f95d00.chunk.js
  monitoring/resources/js/5.1cb3dd5e.chunk.js
  monitoring/resources/js/40.0c9ddbc1.chunk.js
  monitoring/resources/js/main.5a10f92f.chunk.js
  monitoring/resources/js/57.b3628e0e.chunk.js
  monitoring/resources/js/63.76168228.chunk.js
  monitoring/resources/js/62.a5e87602.chunk.js
  monitoring/resources/js/69.d354c945.chunk.js
  monitoring/resources/js/33.f874acc9.chunk.js
  monitoring/resources/js/54.0d770520.chunk.js
  monitoring/resources/js/32.a4bcafec.chunk.js
  monitoring/resources/js/44.a5b7267e.chunk.js
  monitoring/resources/js/51.1eb8cb89.chunk.js
  monitoring/resources/js/7.34d8b7fb.chunk.js
  monitoring/resources/js/41.4c87b95e.chunk.js
  monitoring/resources/js/77.8396f8e8.chunk.js
  monitoring/resources/js/59.1e98d62c.chunk.js
  monitoring/resources/js/78.6d95ceab.chunk.js
  monitoring/resources/js/66.a76b11fa.chunk.js
  monitoring/resources/js/68.82a7b8f6.chunk.js
  monitoring/resources/js/36.810c51c0.chunk.js
  monitoring/resources/js/60.69ebed0d.chunk.js
  monitoring/resources/js/21.28e040b8.chunk.js
  monitoring/resources/js/16.829d825a.chunk.js
  monitoring/resources/js/14.232b4b6e.chunk.js
  monitoring/resources/js/17.1314dfa0.chunk.js
  monitoring/resources/js/50.65300557.chunk.js
  monitoring/resources/js/13.9514e098.chunk.js
  monitoring/resources/js/73.b8e3f76c.chunk.js
  monitoring/resources/js/34.9ac6c889.chunk.js
  monitoring/resources/js/74.048824c9.chunk.js
  monitoring/resources/js/45.e46d1e81.chunk.js
  monitoring/resources/js/48.fd25e54e.chunk.js
  monitoring/resources/js/72.7f46887b.chunk.js
  monitoring/resources/js/58.9be33e32.chunk.js
  monitoring/resources/js/53.6022c001.chunk.js
  monitoring/resources/js/19.a00df727.chunk.js
  monitoring/resources/js/67.7b9bff8b.chunk.js
  monitoring/resources/js/4.7189f7fd.chunk.js
  monitoring/resources/css/main.c9c40502.chunk.css
  monitoring/resources/css/4.3e61f2b6.chunk.css
  monitoring/resources/media/codicon.80a4c25b.ttf
  monitoring/resources/favicon.png
  viewer/index.html
  viewer/viewer.js
  viewer/jstree.min.js
  viewer/style.min.css
  viewer/throbber.gif
  viewer/32px.png
  viewer/40px.png
  viewer/v2/cpu
  viewer/v2/cpu_view.js
  viewer/v2/disk_cell.js
  viewer/v2/disk_map.js
  viewer/v2/index.html
  viewer/v2/man-green.png
  viewer/v2/man-orange.png
  viewer/v2/man-red.png
  viewer/v2/man-yellow.png
  viewer/v2/net_view.js
  viewer/v2/network
  viewer/v2/node_group.js
  viewer/v2/node.js
  viewer/v2/node_map.js
  viewer/v2/nodes
  viewer/v2/node_view.js
  viewer/v2/overview
  viewer/v2/overview.js
  viewer/v2/pdisk.js
  viewer/v2/pool_block.js
  viewer/v2/pool_map.js
  viewer/v2/runner.html
  viewer/v2/stats.js
  viewer/v2/storage
  viewer/v2/storage_group.js
  viewer/v2/storage.js
  viewer/v2/storage_view.js
  viewer/v2/tablet_cell.js
  viewer/v2/tablet_map.js
  viewer/v2/tenant.js
  viewer/v2/tenants
  viewer/v2/tenant_view.js
  viewer/v2/throbber.gif
  viewer/v2/util.js
  viewer/v2/vdisk.js
  viewer/v2/viewer.css
  viewer/v2/viewer.js
  viewer/api/css/print.css
  viewer/api/css/reset.css
  viewer/api/css/screen.css
  viewer/api/css/style.css
  viewer/api/css/typography.css
  viewer/api/fonts/DroidSans-Bold.ttf
  viewer/api/fonts/DroidSans.ttf
  viewer/api/images/collapse.gif
  viewer/api/images/expand.gif
  viewer/api/images/explorer_icons.png
  viewer/api/images/favicon-16x16.png
  viewer/api/images/favicon-32x32.png
  viewer/api/images/favicon.ico
  viewer/api/images/logo_small.png
  viewer/api/images/throbber.gif
  viewer/api/index.html
  viewer/api/lang/ca.js
  viewer/api/lang/en.js
  viewer/api/lang/es.js
  viewer/api/lang/fr.js
  viewer/api/lang/geo.js
  viewer/api/lang/it.js
  viewer/api/lang/ja.js
  viewer/api/lang/ko-kr.js
  viewer/api/lang/pl.js
  viewer/api/lang/pt.js
  viewer/api/lang/ru.js
  viewer/api/lang/tr.js
  viewer/api/lang/translator.js
  viewer/api/lang/zh-cn.js
  viewer/api/lib/backbone-min.js
  viewer/api/lib/es5-shim.js
  viewer/api/lib/handlebars-4.0.5.js
  viewer/api/lib/highlight.9.1.0.pack.js
  viewer/api/lib/highlight.9.1.0.pack_extended.js
  viewer/api/lib/jquery-1.8.0.min.js
  viewer/api/lib/jquery.ba-bbq.min.js
  viewer/api/lib/jquery.slideto.min.js
  viewer/api/lib/jquery.wiggle.min.js
  viewer/api/lib/js-yaml.min.js
  viewer/api/lib/jsoneditor.min.js
  viewer/api/lib/lodash.min.js
  viewer/api/lib/marked.js
  viewer/api/lib/object-assign-pollyfill.js
  viewer/api/lib/sanitize-html.min.js
  viewer/api/lib/swagger-oauth.js
  viewer/api/swagger-ui.min.js
)
