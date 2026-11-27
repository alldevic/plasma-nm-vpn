cmake_minimum_required(VERSION 3.25)
project(pnvprotoui LANGUAGES CXX)

set(QT_MAJOR_VERSION 6)
find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs6)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(KF6 REQUIRED COMPONENTS CoreAddons WidgetsAddons I18n NetworkManagerQt)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBNM REQUIRED IMPORTED_TARGET libnm)
# plasma-nm ships no development files: link its editor library by file name.
find_library(PLASMANM_EDITOR NAMES plasmanm_editor libplasmanm_editor.so.6 REQUIRED)

add_library(plasmanetworkmanagement_pnvprotoui MODULE protoui.cpp)
target_include_directories(plasmanetworkmanagement_pnvprotoui PRIVATE third_party)
target_link_libraries(plasmanetworkmanagement_pnvprotoui
    ${PLASMANM_EDITOR}
    Qt6::Widgets
    KF6::CoreAddons
    KF6::WidgetsAddons
    KF6::I18n
    KF6::NetworkManagerQt
    PkgConfig::LIBNM
)
install(TARGETS plasmanetworkmanagement_pnvprotoui DESTINATION ${KDE_INSTALL_PLUGINDIR}/plasma/network/vpn)
