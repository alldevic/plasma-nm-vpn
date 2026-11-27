add_library(pnv-xray-config STATIC config.c)
target_include_directories(pnv-xray-config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnv-xray-config PUBLIC pnv-profile)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../data/vless.json ${CMAKE_CURRENT_SOURCE_DIR}/../data/vless-extra.list
        ${CMAKE_CURRENT_SOURCE_DIR}/../data/xhttp-extra.list ${CMAKE_CURRENT_SOURCE_DIR}/../data/tls-fingerprint.list
        ${CMAKE_CURRENT_SOURCE_DIR}/../data/xhttp-mode.list DESTINATION share/plasma-nm-vpn/xray)
