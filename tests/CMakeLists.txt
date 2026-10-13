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
        # as qt6ct initializes GTK, which aborts when no display exists. The shell's recovery
        # journals go to the build tree, never to the developer's state directory.
        set_tests_properties(${name} PROPERTIES
            LABELS "${ARG_LABELS}"
            ENVIRONMENT_MODIFICATION "QT_QPA_PLATFORM=set:offscreen;QT_QPA_PLATFORMTHEME=unset:;QT_STYLE_OVERRIDE=unset:;XDG_STATE_HOME=set:${CMAKE_BINARY_DIR}/test-state")
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

archmage_add_test(ColorExperimentTest
    SOURCES core/color/ColorExperimentTest.cpp
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

archmage_add_test(BlurTest
    SOURCES core/render/BlurTest.cpp
    LABELS image
    LINKS archmage_render archmage_commands)

# LeakSanitizer is off for GpuTest: building the compute pipeline makes the NVIDIA driver allocate memory it
# never frees, in driver modules the Vulkan loader unloads before exit, so the reports carry no symbols and
# cannot be suppressed. AddressSanitizer's other checks stay on, and the validation layer reports any Vulkan
# object ArchMage fails to destroy (the tests require zero validation errors).
archmage_add_test(GpuTest
    SOURCES core/render/GpuTest.cpp
    LABELS image
    LINKS archmage_gpu archmage_commands archmage_brush
    ENVIRONMENT ASAN_OPTIONS=detect_leaks=0)

archmage_add_test(CommandsTest
    SOURCES core/commands/CommandsTest.cpp
    LABELS operation
    LINKS archmage_commands)

archmage_add_test(ExecutorTest
    SOURCES core/commands/ExecutorTest.cpp
    LABELS operation
    LINKS archmage_commands)

archmage_add_test(BrushTest
    SOURCES core/brush/BrushTest.cpp
    LABELS math
    LINKS archmage_brush)

archmage_add_test(PaintStrokeTest
    SOURCES core/brush/PaintStrokeTest.cpp
    LABELS operation image
    LINKS archmage_commands archmage_render)

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

archmage_add_test(BrushSliceTest
    SOURCES ui/BrushSliceTest.cpp
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

# Native format (S1-4): writer, reader, save transaction, migrations, AT-001 engine half.
add_library(archmage_native_test_support STATIC io/native/NativeTestSupport.cpp)
target_link_libraries(archmage_native_test_support
    PUBLIC archmage_native archmage_commands archmage_brush archmage_render GTest::gtest)
target_include_directories(archmage_native_test_support PUBLIC io/native)
# A scratch directory on the build tree's filesystem (the home filesystem), next to /tmp (tmpfs).
target_compile_definitions(archmage_native_test_support PRIVATE ARCHMAGE_HOME_SCRATCH="${CMAKE_BINARY_DIR}/test-scratch")
archmage_target_warnings(archmage_native_test_support)

archmage_add_test(NativeFormatTest
    SOURCES io/native/NativeFormatTest.cpp
    LABELS document
    LINKS archmage_native_test_support)

archmage_add_test(NativeMigrationTest
    SOURCES io/native/NativeMigrationTest.cpp
    LABELS document
    LINKS archmage_native_test_support)
target_compile_definitions(NativeMigrationTest PRIVATE ARCHMAGE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/io/native/fixtures")

archmage_add_test(RecoveryTest
    SOURCES io/native/RecoveryTest.cpp
    LABELS document
    LINKS archmage_native_test_support)

archmage_add_test(At001Test
    SOURCES io/native/At001Test.cpp
    LABELS document image
    LINKS archmage_native_test_support)

# Writes fixtures (once per schema minor) and damaged copies for the CLI tests; not run by CTest itself.
add_executable(archmage-native-fixture io/native/FixtureTool.cpp)
target_link_libraries(archmage-native-fixture PRIVATE archmage_native archmage_commands archmage_brush archmage_render)
archmage_target_warnings(archmage-native-fixture)

# CLI subprocess checks: validate and inspect on the fixture, a damaged copy and a missing file.
add_test(NAME cli.native
    COMMAND "${CMAKE_COMMAND}"
        -D "CLI=$<TARGET_FILE:archmage-cli>"
        -D "FIXTURE_TOOL=$<TARGET_FILE:archmage-native-fixture>"
        -D "FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/io/native/fixtures/v0.1.archmage"
        -D "WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-native"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cli/CheckNativeCli.cmake")
set_tests_properties(cli.native PROPERTIES LABELS interchange)

# Resilience (S1-5): fault points, AT-013 kill during save, AT-014 disk exhaustion. The AT tests run
# archmage-cli's hidden self-test subcommands as child processes.
add_library(archmage_child_process STATIC resilience/ChildProcess.cpp)
target_include_directories(archmage_child_process PUBLIC resilience)
archmage_target_warnings(archmage_child_process)

archmage_add_test(FaultPointsTest
    SOURCES resilience/FaultPointsTest.cpp
    LABELS resilience
    LINKS archmage_native_test_support)

archmage_add_test(KillDuringSaveTest
    SOURCES resilience/KillDuringSaveTest.cpp
    LABELS resilience
    LINKS archmage_native_test_support archmage_child_process)
target_compile_definitions(KillDuringSaveTest PRIVATE ARCHMAGE_CLI="$<TARGET_FILE:archmage-cli>")
add_dependencies(KillDuringSaveTest archmage-cli)

archmage_add_test(DiskFullTest
    SOURCES resilience/DiskFullTest.cpp
    LABELS resilience
    LINKS archmage_native_test_support archmage_child_process)
target_compile_definitions(DiskFullTest PRIVATE ARCHMAGE_CLI="$<TARGET_FILE:archmage-cli>")
add_dependencies(DiskFullTest archmage-cli)

# Malformed native corpus (S1-5.7), generated from the fixture at test time.
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
archmage_add_test(NativeCorpusTest
    SOURCES resilience/NativeCorpusTest.cpp
    LABELS resilience
    LINKS archmage_native_test_support archmage_child_process)
target_compile_definitions(NativeCorpusTest PRIVATE
    ARCHMAGE_PYTHON="${Python3_EXECUTABLE}"
    ARCHMAGE_CORPUS_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/resilience/corpus/make_native_corpus.py"
    ARCHMAGE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/io/native/fixtures/v0.1.archmage")

# CLI subprocess check (S1-5.9): recover lists, writes a new file and never overwrites. Needs the fault
# points to make a session die while journaling.
if(ARCHMAGE_FAULT_INJECTION)
    add_test(NAME cli.recover
        COMMAND "${CMAKE_COMMAND}"
            -D "CLI=$<TARGET_FILE:archmage-cli>"
            -D "WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-recover"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cli/CheckRecoverCli.cmake")
    set_tests_properties(cli.recover PROPERTIES LABELS interchange)
endif()
