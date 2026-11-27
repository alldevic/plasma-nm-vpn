add_executable(plasma-nm-vpn-auth-dialog main.c)
target_link_libraries(plasma-nm-vpn-auth-dialog PRIVATE pnv-daemon)
install(TARGETS plasma-nm-vpn-auth-dialog DESTINATION lib/plasma-nm-vpn)
