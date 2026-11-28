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

add_executable(test-xray-config test_xray_config.c)
target_link_libraries(test-xray-config PRIVATE pnv-xray-config)
add_test(NAME xray-config COMMAND test-xray-config ${PROJECT_SOURCE_DIR}/backends/xray/data
         ${PROJECT_SOURCE_DIR}/tests/golden/xray-26.11.9)

add_executable(test-import test_import.c)
target_link_libraries(test-import PRIVATE pnv-profile)
add_test(NAME import COMMAND test-import ${PROJECT_SOURCE_DIR})

add_executable(test-liveness test_liveness.c)
target_link_libraries(test-liveness PRIVATE pnv-daemon)
add_test(NAME liveness COMMAND test-liveness)

if(BUILD_EDITORS)
    add_executable(test-blocks widgets/test_blocks.cpp)
    target_link_libraries(test-blocks PRIVATE pnv-widgets Qt6::Test)
    add_test(NAME blocks COMMAND test-blocks)
    set_tests_properties(blocks PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    add_executable(test-import-widget widgets/test_import_widget.cpp)
    target_compile_definitions(test-import-widget PRIVATE PNV_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(test-import-widget PRIVATE pnv-widgets Qt6::Test)
    add_test(NAME import-widget COMMAND test-import-widget)
    set_tests_properties(import-widget PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    add_executable(test-banner widgets/test_banner.cpp)
    target_link_libraries(test-banner PRIVATE pnv-widgets Qt6::Test)
    add_test(NAME banner COMMAND test-banner)
    set_tests_properties(banner PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

    set(meta_ru_name "Тестовое \"имя\"")
    set(meta_ru_description "Тестовое описание \\ с обратной чертой")
    pnv_editor_metadata(${CMAKE_CURRENT_BINARY_DIR}/test-editor.json
        SERVICE org.freedesktop.NetworkManager.pnv.hysteria2
        NAME "Test" DESCRIPTION "Test editor" NAME_RU "${meta_ru_name}" DESCRIPTION_RU "${meta_ru_description}")
    add_test(NAME editor-metadata COMMAND ${CMAKE_COMMAND} -DFILE=${CMAKE_CURRENT_BINARY_DIR}/test-editor.json
             -DSERVICE=org.freedesktop.NetworkManager.pnv.hysteria2 "-DNAME_RU=${meta_ru_name}"
             "-DDESCRIPTION_RU=${meta_ru_description}" -P ${CMAKE_CURRENT_SOURCE_DIR}/widgets/check-metadata.cmake)
endif()

if(BUILD_EDITORS)
    add_executable(test-hysteria2-editor editors/test_hysteria2_editor.cpp)
    target_compile_definitions(test-hysteria2-editor PRIVATE PNV_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(test-hysteria2-editor PRIVATE pnv-hysteria2-editor Qt6::Test)
    add_test(NAME hysteria2-editor COMMAND test-hysteria2-editor)
    set_tests_properties(hysteria2-editor PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    add_executable(test-vless-editor editors/test_vless_editor.cpp)
    target_compile_definitions(test-vless-editor PRIVATE PNV_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(test-vless-editor PRIVATE pnv-vless-editor Qt6::Test)
    add_test(NAME vless-editor COMMAND test-vless-editor)
    set_tests_properties(vless-editor PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    add_executable(test-awg-editor editors/test_awg_editor.cpp)
    target_compile_definitions(test-awg-editor PRIVATE PNV_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(test-awg-editor PRIVATE pnv-awg-editor Qt6::Test)
    add_test(NAME awg-editor COMMAND test-awg-editor)
    set_tests_properties(awg-editor PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endif()
