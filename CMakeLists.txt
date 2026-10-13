cmake_minimum_required(VERSION 3.28)

project(ArchMage
    VERSION 0.1.0
    DESCRIPTION "Professional image editing and creation application"
    LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported; configure with a preset, e.g. cmake --preset dev-debug")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# D-02: C++20 without compiler extensions.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# ArchMage uses no C++20 modules. Skipping the per-file module scan saves build steps and keeps
# GCC-only module flags out of compile_commands.json, where clang-tidy rejects them.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(ARCHMAGE_BUILD_TESTS "Build the test executables and register them with CTest" ON)
# D-27: named fault points in the save, journal, checkpoint, rename and compaction paths, armed with
# ARCHMAGE_FAULT=<spec>. On in Debug and test presets; the dev-release preset turns it off.
option(ARCHMAGE_FAULT_INJECTION "Compile fault injection points into the durability paths" ON)

include(CompilerWarnings)
include(Sanitizers)
include(Dependencies)
include(ArchMageModules)
include(ArchMageShaders)

add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(ui)
add_subdirectory(apps)
add_subdirectory(benchmarks)

if(ARCHMAGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ARCHMAGE_ENABLE_FUZZ)
    add_subdirectory(fuzz)
endif()
