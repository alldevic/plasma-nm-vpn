add_executable(plasma-nm-vpn-guard main.c)
target_link_libraries(plasma-nm-vpn-guard PRIVATE pnv-daemon)
install(TARGETS plasma-nm-vpn-guard DESTINATION bin)
