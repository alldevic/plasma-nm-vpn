pnv_editor_metadata(${CMAKE_CURRENT_BINARY_DIR}/plasmanetworkmanagement_pnv_hysteria2ui.json
    SERVICE org.freedesktop.NetworkManager.pnv.hysteria2
    NAME "Hysteria2" DESCRIPTION "Hysteria2 VPN connection (plasma-nm-vpn)"
    NAME_RU "Hysteria2" DESCRIPTION_RU "VPN-соединение Hysteria2 (plasma-nm-vpn)")
add_library(pnv-hysteria2-editor STATIC hysteria2widget.cpp)
set_target_properties(pnv-hysteria2-editor PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(pnv-hysteria2-editor PRIVATE TRANSLATION_DOMAIN="plasma-nm-vpn")
target_include_directories(pnv-hysteria2-editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnv-hysteria2-editor PUBLIC pnv-editor-common)

add_library(plasmanetworkmanagement_pnv_hysteria2ui MODULE hysteria2ui.cpp)
target_include_directories(plasmanetworkmanagement_pnv_hysteria2ui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(plasmanetworkmanagement_pnv_hysteria2ui PRIVATE pnv-hysteria2-editor)
pnv_editor_install(plasmanetworkmanagement_pnv_hysteria2ui)
