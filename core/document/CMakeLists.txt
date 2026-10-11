# Document graph, revisions, resource references, invariants (spec §5.5). S1-0 holds only
# the generated Version.h; IDs, revisions and errors arrive in S1-2.
archmage_add_module(document
    SOURCES src/Version.cpp)

# Version.h is rendered into the build tree from Version.h.in: once at configure time so it
# exists before the first build, then before every build to pick up a new git revision.
set(archmage_generated_include "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
set(archmage_version_header "${archmage_generated_include}/archmage/document/Version.h")
set(archmage_version_args
    -D "ARCHMAGE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -D "ARCHMAGE_VERSION=${PROJECT_VERSION}"
    -D "ARCHMAGE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
    -D "ARCHMAGE_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
    -D "ARCHMAGE_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
    -D "INPUT=${CMAKE_CURRENT_SOURCE_DIR}/include/archmage/document/Version.h.in"
    -D "OUTPUT=${archmage_version_header}"
    -P "${PROJECT_SOURCE_DIR}/cmake/GenerateVersion.cmake")

execute_process(COMMAND "${CMAKE_COMMAND}" ${archmage_version_args} COMMAND_ERROR_IS_FATAL ANY)

add_custom_target(archmage_version_header
    COMMAND "${CMAKE_COMMAND}" ${archmage_version_args}
    BYPRODUCTS "${archmage_version_header}"
    COMMENT "Checking the git revision recorded in Version.h"
    VERBATIM)

add_dependencies(archmage_document archmage_version_header)
target_include_directories(archmage_document PUBLIC "${archmage_generated_include}")
