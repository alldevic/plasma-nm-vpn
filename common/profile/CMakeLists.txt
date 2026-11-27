# GLib and json-glib only: the editors and the import parsers link it too (design.md, "plasma-nm user interface").
add_library(pnv-profile STATIC codes.c extra.c list.c schema.c validate.c)
set_target_properties(pnv-profile PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pnv-profile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnv-profile PUBLIC PkgConfig::GLIB PkgConfig::JSONGLIB)
