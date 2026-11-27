add_executable(plasma-nm-vpn-service-sb main.c)
target_link_libraries(plasma-nm-vpn-service-sb PRIVATE pnv-daemon)
install(TARGETS plasma-nm-vpn-service-sb DESTINATION lib/plasma-nm-vpn)
