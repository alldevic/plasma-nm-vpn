add_executable(test-version test_version.c)
target_link_libraries(test-version PRIVATE pnv-daemon)
add_test(NAME version COMMAND test-version)

add_executable(test-profile test_profile.c)
target_link_libraries(test-profile PRIVATE pnv-profile)
add_test(NAME profile COMMAND test-profile)

add_executable(test-server test_server.c)
target_link_libraries(test-server PRIVATE pnv-daemon)
add_test(NAME server COMMAND test-server)

add_executable(test-sb-config test_sb_config.c)
target_link_libraries(test-sb-config PRIVATE pnv-sb-config)
add_test(NAME sb-config COMMAND test-sb-config ${PROJECT_SOURCE_DIR}/backends/sb/data/hysteria2.json
         ${PROJECT_SOURCE_DIR}/backends/sb/data/hysteria2-extra.list ${PROJECT_SOURCE_DIR}/tests/golden/sing-box-1.14.5)

add_executable(test-awg-profile test_awg_profile.c)
target_link_libraries(test-awg-profile PRIVATE pnv-profile)
add_test(NAME awg-profile COMMAND test-awg-profile ${PROJECT_SOURCE_DIR}/backends/awg/data/amneziawg-params.list)

add_executable(test-awg-config test_awg_config.c)
target_link_libraries(test-awg-config PRIVATE pnv-awg-config)
add_test(NAME awg-config COMMAND test-awg-config ${PROJECT_SOURCE_DIR}/backends/awg/data/amneziawg-params.list
         ${PROJECT_SOURCE_DIR}/tests/golden/amneziawg-tools-v3.1.20260812)

add_executable(test-ip4plan test_ip4plan.c)
target_link_libraries(test-ip4plan PRIVATE pnv-daemon)
add_test(NAME ip4plan COMMAND test-ip4plan)
