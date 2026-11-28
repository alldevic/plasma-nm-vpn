pnv_editor_metadata(${CMAKE_CURRENT_BINARY_DIR}/plasmanetworkmanagement_pnv_vlessui.json
    SERVICE org.freedesktop.NetworkManager.pnv.vless
    NAME "VLESS XHTTP Reality" DESCRIPTION "VLESS XHTTP Reality VPN connection (plasma-nm-vpn)"
    NAME_RU "VLESS XHTTP Reality" DESCRIPTION_RU "VPN-соединение VLESS XHTTP Reality (plasma-nm-vpn)")
add_library(pnv-vless-editor STATIC vlesswidget.cpp)
set_target_properties(pnv-vless-editor PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(pnv-vless-editor PRIVATE TRANSLATION_DOMAIN="plasma-nm-vpn")
target_include_directories(pnv-vless-editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnv-vless-editor PUBLIC pnv-editor-common)

add_library(plasmanetworkmanagement_pnv_vlessui MODULE vlessui.cpp)
target_include_directories(plasmanetworkmanagement_pnv_vlessui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(plasmanetworkmanagement_pnv_vlessui PRIVATE pnv-vless-editor)
pnv_editor_install(plasmanetworkmanagement_pnv_vlessui)
