# D-23: the same warning set on every project target and preset; -Werror only when
# ARCHMAGE_WARNINGS_AS_ERRORS is ON (the ci preset). Third-party headers arrive through
# imported targets, which CMake marks as system includes, so their warnings stay silent.

option(ARCHMAGE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (on in the ci preset)" OFF)

function(archmage_target_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
        if(ARCHMAGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
