add_executable(test-version test_version.c)
target_link_libraries(test-version PRIVATE pnv-daemon)
add_test(NAME version COMMAND test-version)
