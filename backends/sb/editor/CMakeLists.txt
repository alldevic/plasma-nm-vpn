pnv_editor_metadata(${CMAKE_CURRENT_BINARY_DIR}/plasmanetworkmanagement_pnv_hysteria2ui.json
    SERVICE org.freedesktop.NetworkManager.pnv.hysteria2
    NAME "Hysteria2" DESCRIPTION "Hysteria2 (plasma-nm-vpn)"
    NAME_RU "Hysteria2" DESCRIPTION_RU "Hysteria2 (plasma-nm-vpn)")
add_library(plasmanetworkmanagement_pnv_hysteria2ui MODULE hysteria2ui.cpp)
target_include_directories(plasmanetworkmanagement_pnv_hysteria2ui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(plasmanetworkmanagement_pnv_hysteria2ui PRIVATE pnv-editor-deps)
install(TARGETS plasmanetworkmanagement_pnv_hysteria2ui DESTINATION ${KDE_INSTALL_PLUGINDIR}/plasma/network/vpn)
