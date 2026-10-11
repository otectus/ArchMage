# Script mode (cmake -P): renders Version.h from its template with the current git revision.
# core/document runs it at configure time and again before every build. configure_file()
# leaves the output untouched when nothing changed, so an unchanged revision recompiles nothing.
#
# Inputs (-D): ARCHMAGE_SOURCE_DIR, ARCHMAGE_VERSION, ARCHMAGE_VERSION_MAJOR,
#              ARCHMAGE_VERSION_MINOR, ARCHMAGE_VERSION_PATCH, INPUT, OUTPUT

foreach(required ARCHMAGE_SOURCE_DIR ARCHMAGE_VERSION INPUT OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "GenerateVersion.cmake: -D ${required}=... is required")
    endif()
endforeach()

# Abbreviated commit, "-dirty" when tracked files differ from it, "unknown" outside git.
set(ARCHMAGE_GIT_REVISION "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${ARCHMAGE_SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE revision
        RESULT_VARIABLE revision_result
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(revision_result EQUAL 0 AND revision)
        set(ARCHMAGE_GIT_REVISION "${revision}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${ARCHMAGE_SOURCE_DIR}" status --porcelain --untracked-files=no
            OUTPUT_VARIABLE changes
            RESULT_VARIABLE status_result
            ERROR_QUIET)
        if(status_result EQUAL 0 AND NOT changes STREQUAL "")
            string(APPEND ARCHMAGE_GIT_REVISION "-dirty")
        endif()
    endif()
endif()

configure_file("${INPUT}" "${OUTPUT}" @ONLY)
