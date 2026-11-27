add_library(pnv-xray-config STATIC config.c)
target_include_directories(pnv-xray-config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnv-xray-config PUBLIC pnv-profile)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../data/vless.json ${CMAKE_CURRENT_SOURCE_DIR}/../data/vless-extra.list
        ${CMAKE_CURRENT_SOURCE_DIR}/../data/xhttp-extra.list ${CMAKE_CURRENT_SOURCE_DIR}/../data/tls-fingerprint.list
        ${CMAKE_CURRENT_SOURCE_DIR}/../data/xhttp-mode.list DESTINATION share/plasma-nm-vpn/xray)

add_executable(plasma-nm-vpn-service-xray main.c)
target_link_libraries(plasma-nm-vpn-service-xray PRIVATE pnv-daemon pnv-xray-config)
install(TARGETS plasma-nm-vpn-service-xray DESTINATION lib/plasma-nm-vpn)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../data/plasma-nm-vpn-vless.name DESTINATION lib/NetworkManager/VPN)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../data/plasma-nm-vpn-xray.conf DESTINATION share/dbus-1/system.d)
