add_executable(test-version test_version.c)
target_link_libraries(test-version PRIVATE pnv-daemon)
add_test(NAME version COMMAND test-version)

add_executable(test-profile test_profile.c)
target_link_libraries(test-profile PRIVATE pnv-profile)
add_test(NAME profile COMMAND test-profile)
