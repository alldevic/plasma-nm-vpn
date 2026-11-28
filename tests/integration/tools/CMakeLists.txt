# Test build only: the integration cases' helpers, never in a package.
add_executable(pnv-banner-form banner-form.cpp)
target_link_libraries(pnv-banner-form PRIVATE pnv-widgets)
# Installed next to the packaged widget library, which it then uses.
set_target_properties(pnv-banner-form PROPERTIES INSTALL_RPATH ${KDE_INSTALL_FULL_LIBDIR}/plasma-nm-vpn)
install(TARGETS pnv-banner-form DESTINATION ${CMAKE_INSTALL_LIBDIR}/plasma-nm-vpn/test)

add_executable(pnv-editor-form editor-form.cpp)
target_link_libraries(pnv-editor-form PRIVATE pnv-editor-deps pnv-widgets)
# Installed next to the packaged widget library, which it then uses.
set_target_properties(pnv-editor-form PROPERTIES INSTALL_RPATH ${KDE_INSTALL_FULL_LIBDIR}/plasma-nm-vpn)
install(TARGETS pnv-editor-form DESTINATION ${CMAKE_INSTALL_LIBDIR}/plasma-nm-vpn/test)
