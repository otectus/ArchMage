# CTest script (cmake -P): archmage-cli --version must exit 0 and print exactly
# "ArchMage <version> (<revision>)" followed by a newline.
#
# Inputs (-D): CLI (path to archmage-cli), EXPECTED_VERSION (the CMake project version)

execute_process(
    COMMAND "${CLI}" --version
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    RESULT_VARIABLE result)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "archmage-cli --version exited with '${result}'; stderr: ${errors}")
endif()

string(REPLACE "." "\\." version_pattern "${EXPECTED_VERSION}")
if(NOT output MATCHES "^ArchMage ${version_pattern} \\(([0-9a-f]+(-dirty)?|unknown)\\)\n$")
    message(FATAL_ERROR "Unexpected version banner: '${output}'")
endif()

message(STATUS "archmage-cli --version: ${output}")
