# CTest script (cmake -P), plan S1-4.8: archmage-cli validate and inspect on the committed fixture, on a
# copy with one damaged tile blob, on a file that is not a document and on a missing file. Exit codes:
# 0 ok, 1 invalid, 2 error; --json prints one JSON document with structured error records.
#
# Inputs (-D): CLI, FIXTURE_TOOL, FIXTURE, WORK_DIR

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(run_cli expected_exit out_var)
    execute_process(COMMAND "${CLI}" ${ARGN}
        OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE result)
    if(NOT result EQUAL expected_exit)
        message(FATAL_ERROR "archmage-cli ${ARGN}: exit ${result}, expected ${expected_exit}\nstdout: ${output}\nstderr: ${errors}")
    endif()
    set(${out_var} "${output}" PARENT_SCOPE)
endfunction()

function(expect_json json expected)
    string(JSON value ERROR_VARIABLE json_error GET "${json}" ${ARGN})
    if(json_error)
        message(FATAL_ERROR "JSON path ${ARGN}: ${json_error}\n${json}")
    endif()
    if(NOT value STREQUAL expected)
        message(FATAL_ERROR "JSON path ${ARGN}: '${value}', expected '${expected}'")
    endif()
endfunction()

# The fixture is valid at both levels.
run_cli(0 out validate "${FIXTURE}")
if(NOT out MATCHES "^valid: ")
    message(FATAL_ERROR "unexpected validate output: ${out}")
endif()
run_cli(0 out validate "${FIXTURE}" --deep --json)
expect_json("${out}" "ON" ok)
expect_json("${out}" "deep" level)
expect_json("${out}" "0" schema major)
string(JSON verified GET "${out}" counts resourcesVerified)
if(verified LESS 2)
    message(FATAL_ERROR "deep validation verified ${verified} resources")
endif()

# A damaged tile blob is invalid input with a damaged_input record.
execute_process(COMMAND "${FIXTURE_TOOL}" corrupt-tile "${FIXTURE}" "${WORK_DIR}/damaged.archmage" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "fixture tool failed: ${result}")
endif()
run_cli(1 out validate "${WORK_DIR}/damaged.archmage" --json)
expect_json("${out}" "OFF" ok)
expect_json("${out}" "damaged_input" problems 0 category)
run_cli(1 out validate "${WORK_DIR}/damaged.archmage")
if(NOT out MATCHES "^invalid: ")
    message(FATAL_ERROR "unexpected validate output: ${out}")
endif()

# Not a document: invalid. Missing: an error with a JSON error record.
file(WRITE "${WORK_DIR}/notes.archmage" "plain text, not an ArchMage document, padded to look like a header....")
run_cli(1 out validate "${WORK_DIR}/notes.archmage" --json)
run_cli(1 out inspect "${WORK_DIR}/notes.archmage" --json)
run_cli(2 out validate "${WORK_DIR}/missing.archmage" --json)
expect_json("${out}" "OFF" ok)
expect_json("${out}" "permission_denied" error category)
expect_json("${out}" "file.not_found" error code)
run_cli(2 out inspect "${WORK_DIR}/missing.archmage")

# inspect describes the fixture.
run_cli(0 out inspect "${FIXTURE}" --json)
expect_json("${out}" "0" schema major)
expect_json("${out}" "1" schema minor)
expect_json("${out}" "Ink" layers 1 name)
expect_json("${out}" "4" objectTypes layer.raster)
run_cli(0 out inspect "${FIXTURE}")
if(NOT out MATCHES "ArchMage document, format 0\\.1")
    message(FATAL_ERROR "unexpected inspect output: ${out}")
endif()

# Usage errors exit 2.
run_cli(2 out validate)

message(STATUS "archmage-cli validate and inspect: ok")
