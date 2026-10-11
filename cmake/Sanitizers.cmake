# D-03: sanitizer and fuzzing builds. The presets set these cache variables; they apply to
# every target so libraries, executables and tests are instrumented consistently.
#
#   ARCHMAGE_SANITIZERS   list of address, undefined, thread (asan-ubsan: address;undefined)
#   ARCHMAGE_ENABLE_FUZZ  libFuzzer coverage instrumentation plus the fuzz/ targets (Clang only)

set(ARCHMAGE_SANITIZERS "" CACHE STRING "Sanitizers for every target: a list of address, undefined, thread")
option(ARCHMAGE_ENABLE_FUZZ "Build libFuzzer targets under fuzz/ (fuzz preset, Clang only)" OFF)

if(ARCHMAGE_SANITIZERS)
    foreach(sanitizer IN LISTS ARCHMAGE_SANITIZERS)
        if(NOT sanitizer MATCHES "^(address|undefined|thread)$")
            message(FATAL_ERROR "Unknown sanitizer '${sanitizer}' in ARCHMAGE_SANITIZERS")
        endif()
    endforeach()
    if("thread" IN_LIST ARCHMAGE_SANITIZERS AND "address" IN_LIST ARCHMAGE_SANITIZERS)
        message(FATAL_ERROR "ThreadSanitizer cannot be combined with AddressSanitizer")
    endif()

    list(JOIN ARCHMAGE_SANITIZERS "," sanitizer_flags)
    add_compile_options(-fsanitize=${sanitizer_flags} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${sanitizer_flags})
    if("undefined" IN_LIST ARCHMAGE_SANITIZERS)
        # Undefined behavior fails the test instead of printing and continuing.
        add_compile_options(-fno-sanitize-recover=all)
    endif()
endif()

if(ARCHMAGE_ENABLE_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "ARCHMAGE_ENABLE_FUZZ needs Clang's libFuzzer; use the fuzz preset")
    endif()
    # Coverage instrumentation everywhere; only fuzz targets link the libFuzzer driver.
    add_compile_options(-fsanitize=fuzzer-no-link)
endif()
