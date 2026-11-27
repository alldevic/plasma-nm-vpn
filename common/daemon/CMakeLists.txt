add_library(pnv-daemon STATIC core.c plugin.c rundir.c secrets.c tun.c units.c version.c)
target_include_directories(pnv-daemon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(pnv-daemon PRIVATE PNV_VERSION="${PROJECT_VERSION}" PUBLIC G_LOG_DOMAIN="plasma-nm-vpn")
target_link_libraries(pnv-daemon PUBLIC pnv-profile PkgConfig::LIBNM PkgConfig::GLIB PkgConfig::JSONGLIB)
