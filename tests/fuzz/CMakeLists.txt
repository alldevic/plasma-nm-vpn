# libFuzzer targets (clang): cmake -DCMAKE_C_COMPILER=clang -DPNV_FUZZ=ON; run with the seeds in corpus/.
foreach(target profile extra)
    add_executable(fuzz-${target} fuzz_${target}.c)
    target_link_libraries(fuzz-${target} PRIVATE pnv-profile)
    target_link_options(fuzz-${target} PRIVATE -fsanitize=fuzzer)
endforeach()
