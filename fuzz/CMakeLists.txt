# libFuzzer harnesses, configured only by the fuzz preset (ARCHMAGE_ENABLE_FUZZ=ON, Clang). Runs and
# results are recorded in docs/testing/fuzz.md. The PNG harness arrives with the PNG codec (S1-8).
add_custom_target(fuzzers)

add_executable(native_reader_fuzz native_reader_fuzz.cpp)
target_link_libraries(native_reader_fuzz PRIVATE archmage_native)
target_link_options(native_reader_fuzz PRIVATE -fsanitize=fuzzer)
archmage_target_warnings(native_reader_fuzz)
add_dependencies(fuzzers native_reader_fuzz)
