# Shared by the editors in the core packages; uses plasma-nm's API, so it is linked into each editor module.
add_library(pnv-editor-common STATIC pnv-editor.cpp)
set_target_properties(pnv-editor-common PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(pnv-editor-common PRIVATE TRANSLATION_DOMAIN="plasma-nm-vpn")
target_include_directories(pnv-editor-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnv-editor-common PUBLIC pnv-editor-deps pnv-widgets)
