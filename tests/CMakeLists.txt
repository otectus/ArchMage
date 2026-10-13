# Test tiers (D-05, spec §27.1). Every test carries one or more of these CTest labels; one
# reproducible entry point per tier: ctest --preset <preset> -L <label>
set(ARCHMAGE_TEST_LABELS math operation document image ui interchange resilience perf)

include(GoogleTest)

# archmage_add_test(<name> SOURCES <file>... LABELS <tier>... [LINKS <target>...] [QT]
#                   [ENVIRONMENT <VAR=value>...])
#
# GoogleTest executables register each TEST() as its own CTest test. QT marks a Qt Test
# executable (QTEST_MAIN entry point, AUTOMOC); it runs as one CTest test on the offscreen
# platform, so the UI tier needs no display server.
function(archmage_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "QT" "" "SOURCES;LABELS;LINKS;ENVIRONMENT")
    if(NOT ARG_SOURCES OR NOT ARG_LABELS)
        message(FATAL_ERROR "archmage_add_test(${name}): SOURCES and LABELS are required")
    endif()
    foreach(label IN LISTS ARG_LABELS)
        if(NOT label IN_LIST ARCHMAGE_TEST_LABELS)
            message(FATAL_ERROR "archmage_add_test(${name}): unknown label '${label}'; use one of ${ARCHMAGE_TEST_LABELS}")
        endif()
    endforeach()

    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_LINKS})
    archmage_target_warnings(${name})

    if(ARG_QT)
        target_link_libraries(${name} PRIVATE Qt6::Test)
        set_target_properties(${name} PROPERTIES AUTOMOC ON)
        add_test(NAME ${name} COMMAND ${name})
        # Desktop theming from the developer's session must not leak in: a platform theme such
        # as qt6ct initializes GTK, which aborts when no display exists.
        set_tests_properties(${name} PROPERTIES
            LABELS "${ARG_LABELS}"
            ENVIRONMENT_MODIFICATION "QT_QPA_PLATFORM=set:offscreen;QT_QPA_PLATFORMTHEME=unset:;QT_STYLE_OVERRIDE=unset:")
        if(ARG_ENVIRONMENT)
            set_tests_properties(${name} PROPERTIES ENVIRONMENT "${ARG_ENVIRONMENT}")
        endif()
    else()
        target_link_libraries(${name} PRIVATE GTest::gtest GTest::gtest_main)
        # Property values travel through a generated script; escape list separators so a
        # multi-value LABELS or ENVIRONMENT stays one value.
        string(REPLACE ";" "\\;" labels "${ARG_LABELS}")
        set(properties LABELS "${labels}")
        if(ARG_ENVIRONMENT)
            string(REPLACE ";" "\\;" environment "${ARG_ENVIRONMENT}")
            list(APPEND properties ENVIRONMENT "${environment}")
        endif()
        # PRE_TEST discovery lists the cases when ctest runs instead of executing freshly
        # built (possibly sanitized) binaries during the build.
        gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST PROPERTIES ${properties})
    endif()
endfunction()

archmage_add_test(VersionTest
    SOURCES core/VersionTest.cpp
    LABELS document
    LINKS archmage_core)
target_compile_definitions(VersionTest PRIVATE ARCHMAGE_EXPECTED_VERSION="${PROJECT_VERSION}")

archmage_add_test(BaseTest
    SOURCES
        core/base/ExpectedTest.cpp
        core/base/IdTest.cpp
        core/base/GeometryTest.cpp
    LABELS math
    LINKS archmage_base)

archmage_add_test(TilesTest
    SOURCES core/tiles/TilesTest.cpp
    LABELS math
    LINKS archmage_tiles)

archmage_add_test(ColorTest
    SOURCES core/color/ColorTest.cpp
    LABELS math
    LINKS archmage_color LCMS2::LCMS2)

archmage_add_test(DocumentTest
    SOURCES core/document/DocumentTest.cpp
    LABELS document
    LINKS archmage_document)

archmage_add_test(KernelsTest
    SOURCES core/render/KernelsTest.cpp
    LABELS math
    LINKS archmage_render)

archmage_add_test(EvaluatorTest
    SOURCES core/render/EvaluatorTest.cpp
    LABELS image
    LINKS archmage_render)

archmage_add_test(CommandsTest
    SOURCES core/commands/CommandsTest.cpp
    LABELS operation
    LINKS archmage_commands)

archmage_add_test(ExecutorTest
    SOURCES core/commands/ExecutorTest.cpp
    LABELS operation
    LINKS archmage_commands)

archmage_add_test(ViewTransformTest
    SOURCES core/ViewTransformTest.cpp
    LABELS math
    LINKS archmage_render)

archmage_add_test(SmokeTest
    SOURCES ui/SmokeTest.cpp
    LABELS ui
    LINKS archmage_ui
    QT)

archmage_add_test(InputMapperTest
    SOURCES ui/InputMapperTest.cpp
    LABELS ui
    LINKS archmage_ui
    QT)

archmage_add_test(CanvasSurfaceTest
    SOURCES ui/CanvasSurfaceTest.cpp
    LABELS ui
    LINKS archmage_ui
    QT)

# CLI subprocess check: archmage-cli --version exits 0 and prints the version banner.
add_test(NAME cli.version
    COMMAND "${CMAKE_COMMAND}"
        -D "CLI=$<TARGET_FILE:archmage-cli>"
        -D "EXPECTED_VERSION=${PROJECT_VERSION}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cli/CheckVersion.cmake")
set_tests_properties(cli.version PROPERTIES LABELS interchange)
