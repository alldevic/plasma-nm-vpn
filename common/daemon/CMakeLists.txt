add_library(pnv-daemon STATIC version.c)
target_include_directories(pnv-daemon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(pnv-daemon PRIVATE PNV_VERSION="${PROJECT_VERSION}")
target_link_libraries(pnv-daemon PUBLIC PkgConfig::LIBNM PkgConfig::GLIB PkgConfig::JSONGLIB)
