add_library(pnv-sb-config STATIC config.c)
target_include_directories(pnv-sb-config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnv-sb-config PUBLIC pnv-profile)

add_executable(plasma-nm-vpn-service-sb main.c)
target_link_libraries(plasma-nm-vpn-service-sb PRIVATE pnv-daemon pnv-sb-config)
install(TARGETS plasma-nm-vpn-service-sb DESTINATION lib/plasma-nm-vpn)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../data/plasma-nm-vpn-hysteria2.name DESTINATION lib/NetworkManager/VPN)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../data/plasma-nm-vpn-sb.conf DESTINATION share/dbus-1/system.d)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../data/hysteria2.json ${CMAKE_CURRENT_SOURCE_DIR}/../data/hysteria2-extra.list
        DESTINATION share/plasma-nm-vpn/sb)
