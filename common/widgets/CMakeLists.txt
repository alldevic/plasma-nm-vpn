# The shared form blocks of the editors (6.1). No plasma-nm API, so not tied to a plasma-nm version; a private
# library of plasma-nm-vpn-common, which the core packages depend on at the exact version.
ki18n_wrap_ui(PNV_WIDGETS_UI serverblock.ui tlsblock.ui protectionblock.ui advancedblock.ui)
add_library(pnv-widgets SHARED
    block.cpp messages.cpp serverblock.cpp tlsblock.cpp authblock.cpp protectionblock.cpp advancedblock.cpp
    guardstatus.cpp killswitchbanner.cpp importwidget.cpp exportdialog.cpp
    ${PNV_WIDGETS_UI})
generate_export_header(pnv-widgets BASE_NAME pnv_widgets)
# GLib has members named signals and slots.
target_compile_definitions(pnv-widgets PRIVATE TRANSLATION_DOMAIN="plasma-nm-vpn" PUBLIC QT_NO_KEYWORDS)
target_include_directories(pnv-widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(pnv-widgets PUBLIC Qt6::Widgets KF6::WidgetsAddons KF6::NetworkManagerQt pnv-profile
    PRIVATE KF6::I18n)
install(TARGETS pnv-widgets LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR}/plasma-nm-vpn)
