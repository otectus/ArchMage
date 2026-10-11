# libFuzzer harnesses, configured only by the fuzz preset (ARCHMAGE_ENABLE_FUZZ=ON, Clang).
# The native-reader and PNG harnesses arrive in S1-5.8; the aggregate target exists now.
add_custom_target(fuzzers)
