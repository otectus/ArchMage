# CTest script (cmake -P), plan S1-5.9: archmage-cli recover lists the documents an interrupted session
# left, writes one to a new file, and never overwrites an existing file.
#
# Inputs (-D): CLI, WORK_DIR

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(root "${WORK_DIR}/root")

function(run_cli expected_exit out_var)
    execute_process(COMMAND "${CLI}" ${ARGN}
        OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE result)
    if(NOT result EQUAL expected_exit)
        message(FATAL_ERROR "archmage-cli ${ARGN}: exit ${result}, expected ${expected_exit}\nstdout: ${output}\nstderr: ${errors}")
    endif()
    set(${out_var} "${output}" PARENT_SCOPE)
endfunction()

# Nothing to recover yet.
run_cli(0 out recover --root "${root}" --json)
string(JSON count LENGTH "${out}" candidates)
if(NOT count EQUAL 0)
    message(FATAL_ERROR "expected no candidates: ${out}")
endif()

# A session that dies while journaling (the self-test child, killed at the journal's commit).
run_cli(137 out selftest-save --root "${root}" --target "${WORK_DIR}/doc.archmage" --fault tx_commit --phase journal)

run_cli(0 out recover --root "${root}" --json)
string(JSON count LENGTH "${out}" candidates)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "expected one candidate: ${out}")
endif()
string(JSON session GET "${out}" candidates 0 session)
string(JSON document GET "${out}" candidates 0 document)
string(JSON sequence GET "${out}" candidates 0 revision sequence)
string(JSON source GET "${out}" candidates 0 source)
if(NOT source STREQUAL "${WORK_DIR}/doc.archmage")
    message(FATAL_ERROR "unexpected source path '${source}'")
endif()
run_cli(0 out recover --root "${root}")
if(NOT out MATCHES "${session}")
    message(FATAL_ERROR "the list does not show the session: ${out}")
endif()

# Recover to a new file; the original save is untouched.
file(SHA256 "${WORK_DIR}/doc.archmage" original_before)
run_cli(0 out recover --root "${root}" --session "${session}" --output "${WORK_DIR}/recovered.archmage" --json)
string(JSON written GET "${out}" document)
if(NOT written STREQUAL document)
    message(FATAL_ERROR "recovered the wrong document: ${out}")
endif()
run_cli(0 out validate --deep "${WORK_DIR}/recovered.archmage")
run_cli(0 out inspect --json "${WORK_DIR}/recovered.archmage")
string(JSON recovered_sequence GET "${out}" revisions 0 sequence)
if(NOT recovered_sequence EQUAL sequence)
    message(FATAL_ERROR "recovered revision ${recovered_sequence}, the journal held ${sequence}")
endif()
file(SHA256 "${WORK_DIR}/doc.archmage" original_after)
if(NOT original_before STREQUAL original_after)
    message(FATAL_ERROR "recover changed the original")
endif()

# Never overwrite: an existing output is refused and left as it was.
file(SHA256 "${WORK_DIR}/recovered.archmage" recovered_before)
run_cli(2 out recover --root "${root}" --session "${session}" --output "${WORK_DIR}/recovered.archmage" --json)
string(JSON code GET "${out}" error code)
if(NOT code STREQUAL "recover.output_exists")
    message(FATAL_ERROR "unexpected error: ${out}")
endif()
file(SHA256 "${WORK_DIR}/recovered.archmage" recovered_after)
if(NOT recovered_before STREQUAL recovered_after)
    message(FATAL_ERROR "recover overwrote an existing file")
endif()

# Unknown session, missing output.
run_cli(2 out recover --root "${root}" --session sess-00000000000000000000000000 --output "${WORK_DIR}/x.archmage")
run_cli(2 out recover --root "${root}" --session "${session}")

message(STATUS "archmage-cli recover: ok")
