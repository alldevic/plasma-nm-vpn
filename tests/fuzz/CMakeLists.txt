# libFuzzer targets (clang): cmake -DCMAKE_C_COMPILER=clang -DPNV_FUZZ=ON; run with the seeds in corpus/.
foreach(target profile extra)
    add_executable(fuzz-${target} fuzz_${target}.c)
    target_link_libraries(fuzz-${target} PRIVATE pnv-profile)
    target_link_options(fuzz-${target} PRIVATE -fsanitize=fuzzer)
endforeach()

# The import parsers (6.6): one target per format, the input following the format's opening.
foreach(format "hysteria2|hy2://" "vless|vless://" "amneziawg|[Interface]\\n")
    string(REPLACE "|" ";" pair "${format}")
    list(GET pair 0 name)
    list(GET pair 1 prefix)
    add_executable(fuzz-import-${name} fuzz_import.c)
    target_compile_definitions(fuzz-import-${name} PRIVATE "FUZZ_PREFIX=\"${prefix}\"")
    target_link_libraries(fuzz-import-${name} PRIVATE pnv-profile)
    target_link_options(fuzz-import-${name} PRIVATE -fsanitize=fuzzer)
endforeach()
