
# This file was gererated by the build system used internally in the Yandex monorepo.
# Only simple modifications are allowed (adding source-files to targets, adding simple properties
# like target_include_directories). These modifications will be ported to original
# ya.make files by maintainers. Any complex modifications which can't be ported back to the
# original buildsystem will not be accepted.



add_library(cpp-lwtrace-mon)
target_link_libraries(cpp-lwtrace-mon PUBLIC
  contrib-libs-cxxsupp
  yutil
  library-cpp-resource
  cpp-html-pcdata
  library-cpp-lwtrace
  lwtrace-mon-analytics
  cpp-monlib-dynamic_counters
  cpp-string_utils-base64
)
target_sources(cpp-lwtrace-mon PRIVATE
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/mon_lwtrace.cpp
)

add_global_library_for(cpp-lwtrace-mon.global cpp-lwtrace-mon)
target_link_libraries(cpp-lwtrace-mon.global PUBLIC
  contrib-libs-cxxsupp
  yutil
  library-cpp-resource
  cpp-html-pcdata
  library-cpp-lwtrace
  lwtrace-mon-analytics
  cpp-monlib-dynamic_counters
  cpp-string_utils-base64
)
target_sources(cpp-lwtrace-mon.global PRIVATE
  ${CMAKE_BINARY_DIR}/library/cpp/lwtrace/mon/55ff1192d5e8c2c093974f8d6c720a35.cpp
)
resources(cpp-lwtrace-mon.global
  ${CMAKE_BINARY_DIR}/library/cpp/lwtrace/mon/55ff1192d5e8c2c093974f8d6c720a35.cpp
  INPUTS
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/common.css
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/common.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/css/bootstrap.min.css
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/css/d3-gantt.css
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/css/jquery.treegrid.css
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/analytics.css
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/analytics.flot.html
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/analytics.gantt.html
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/analytics.header.html
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/analytics.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/fonts/glyphicons-halflings-regular.eot
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/fonts/glyphicons-halflings-regular.svg
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/fonts/glyphicons-halflings-regular.ttf
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/fonts/glyphicons-halflings-regular.woff2
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/fonts/glyphicons-halflings-regular.woff
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/footer.html
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/header.html
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/img/collapse.png
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/img/expand.png
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/img/file.png
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/img/folder.png
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/bootstrap.min.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/d3.v4.min.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/d3-gantt.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/d3-tip-0.8.0-alpha.1.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/filesaver.min.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/jquery.flot.extents.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/jquery.flot.min.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/jquery.flot.navigate.min.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/jquery.flot.selection.min.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/jquery.min.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/jquery.treegrid.bootstrap3.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/jquery.treegrid.min.js
  ${CMAKE_SOURCE_DIR}/library/cpp/lwtrace/mon/static/js/jquery.url.min.js
  KEYS
  lwtrace/mon/static/common.css
  lwtrace/mon/static/common.js
  lwtrace/mon/static/css/bootstrap.min.css
  lwtrace/mon/static/css/d3-gantt.css
  lwtrace/mon/static/css/jquery.treegrid.css
  lwtrace/mon/static/analytics.css
  lwtrace/mon/static/analytics.flot.html
  lwtrace/mon/static/analytics.gantt.html
  lwtrace/mon/static/analytics.header.html
  lwtrace/mon/static/analytics.js
  lwtrace/mon/static/fonts/glyphicons-halflings-regular.eot
  lwtrace/mon/static/fonts/glyphicons-halflings-regular.svg
  lwtrace/mon/static/fonts/glyphicons-halflings-regular.ttf
  lwtrace/mon/static/fonts/glyphicons-halflings-regular.woff2
  lwtrace/mon/static/fonts/glyphicons-halflings-regular.woff
  lwtrace/mon/static/footer.html
  lwtrace/mon/static/header.html
  lwtrace/mon/static/img/collapse.png
  lwtrace/mon/static/img/expand.png
  lwtrace/mon/static/img/file.png
  lwtrace/mon/static/img/folder.png
  lwtrace/mon/static/js/bootstrap.min.js
  lwtrace/mon/static/js/d3.v4.min.js
  lwtrace/mon/static/js/d3-gantt.js
  lwtrace/mon/static/js/d3-tip-0.8.0-alpha.1.js
  lwtrace/mon/static/js/filesaver.min.js
  lwtrace/mon/static/js/jquery.flot.extents.js
  lwtrace/mon/static/js/jquery.flot.min.js
  lwtrace/mon/static/js/jquery.flot.navigate.min.js
  lwtrace/mon/static/js/jquery.flot.selection.min.js
  lwtrace/mon/static/js/jquery.min.js
  lwtrace/mon/static/js/jquery.treegrid.bootstrap3.js
  lwtrace/mon/static/js/jquery.treegrid.min.js
  lwtrace/mon/static/js/jquery.url.min.js
)
