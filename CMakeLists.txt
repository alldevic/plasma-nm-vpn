cmake_minimum_required(VERSION 3.25)
project(plasma-nm-vpn VERSION 0.1.0 LANGUAGES C CXX)

option(BUILD_EDITORS "Build the plasma-nm editor plugins" ON)
# The test build: hooks for the integration tests (design.md, "Testing"). Never in a package.
option(PNV_TEST_HOOKS "Compile in the test hooks" OFF)
option(PNV_FUZZ "Build the fuzz targets (clang, sanitizers)" OFF)
include(CTest)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options("$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wno-missing-field-initializers>")
if(PNV_TEST_HOOKS)
    add_compile_definitions(PNV_TEST_HOOKS)
endif()
if(PNV_FUZZ)
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all -g)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBNM REQUIRED IMPORTED_TARGET libnm)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0)
pkg_check_modules(JSONGLIB REQUIRED IMPORTED_TARGET json-glib-1.0)

add_subdirectory(common/profile)
add_subdirectory(common/daemon)
add_subdirectory(common/auth-dialog)
add_subdirectory(common/guard)
add_subdirectory(common/tun-fd)
add_subdirectory(backends/sb/daemon)
add_subdirectory(backends/awg/daemon)
add_subdirectory(backends/xray/daemon)

if(BUILD_EDITORS)
    # ECM looks for Qt 5's qmake unless told otherwise.
    set(QT_MAJOR_VERSION 6)
    find_package(ECM REQUIRED NO_MODULE)
    list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
    include(KDEInstallDirs6)
    include(KDECMakeSettings)
    include(KDECompilerSettings NO_POLICY_SCOPE)

    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    if(BUILD_TESTING)
        find_package(Qt6 REQUIRED COMPONENTS Test)
    endif()
    find_package(KF6 REQUIRED COMPONENTS CoreAddons WidgetsAddons I18n NetworkManagerQt)
    # plasma-nm ships no development files: the editors use the headers in third_party/plasma-nm
    # and link only against its editor library.
    find_library(PLASMANM_EDITOR NAMES plasmanm_editor REQUIRED)

    add_library(pnv-editor-deps INTERFACE)
    target_include_directories(pnv-editor-deps INTERFACE ${PROJECT_SOURCE_DIR}/third_party/plasma-nm)
    target_link_libraries(pnv-editor-deps INTERFACE
        ${PLASMANM_EDITOR} Qt6::Widgets KF6::CoreAddons KF6::WidgetsAddons KF6::I18n KF6::NetworkManagerQt
        PkgConfig::LIBNM)

    include(GenerateExportHeader)
    include(${PROJECT_SOURCE_DIR}/cmake/PnvEditor.cmake)
    add_subdirectory(common/widgets)
    add_subdirectory(backends/sb/editor)
    if(PNV_TEST_HOOKS)
        add_subdirectory(tests/integration/tools)
    endif()
endif()

if(BUILD_TESTING)
    add_subdirectory(tests/unit)
endif()
if(PNV_FUZZ)
    add_subdirectory(tests/fuzz)
endif()
