pnv_editor_metadata(${CMAKE_CURRENT_BINARY_DIR}/plasmanetworkmanagement_pnv_amneziawgui.json
    SERVICE org.freedesktop.NetworkManager.pnv.amneziawg
    NAME "AmneziaWG" DESCRIPTION "AmneziaWG VPN connection (plasma-nm-vpn)"
    NAME_RU "AmneziaWG" DESCRIPTION_RU "VPN-соединение AmneziaWG (plasma-nm-vpn)")
add_library(pnv-awg-editor STATIC awgwidget.cpp)
set_target_properties(pnv-awg-editor PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(pnv-awg-editor PRIVATE TRANSLATION_DOMAIN="plasma-nm-vpn")
target_include_directories(pnv-awg-editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnv-awg-editor PUBLIC pnv-editor-common)

add_library(plasmanetworkmanagement_pnv_amneziawgui MODULE awgui.cpp)
target_include_directories(plasmanetworkmanagement_pnv_amneziawgui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(plasmanetworkmanagement_pnv_amneziawgui PRIVATE pnv-awg-editor)
pnv_editor_install(plasmanetworkmanagement_pnv_amneziawgui)
