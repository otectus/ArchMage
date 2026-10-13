# Third-party dependencies (D-04): system packages located through CMake package configs or
# find modules, with pkg-config imported targets for libraries that ship no usable CMake
# config. Link the imported targets only; never hardcode distribution paths.
#
# Every package looked up here has an entry in docs/dependencies/manifest.yaml (source,
# pinned version, checksum, license, redistribution, maintenance, affected features);
# tests/docs/manifest_test.py fails when one is missing.

# Desktop shell and UI tests (D-05, D-11). Qt 6.7 is the floor for the QRhiWidget canvas candidate.
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets Test Svg)
# Canvas adapter experiment (S1-1, D-11): QRhiWidget needs the QRhi headers, which Qt ships only with
# its private Gui module, and qsb (ShaderTools) to package its shaders. The private-module use is
# deliberate and recorded in ADR-0011, so Qt's configure-time reminder about it is silenced.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.7 REQUIRED COMPONENTS GuiPrivate ShaderTools)

# Native container, content hashing and blob compression (D-13, D-19).
find_package(SQLite3 3.40 REQUIRED)
find_package(zstd 1.5 REQUIRED CONFIG)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)

# PNG and JPEG codecs (D-18).
find_package(ZLIB REQUIRED)
find_package(PNG 1.6 REQUIRED)
find_package(JPEG REQUIRED)

# ICC profiles (D-10). Little CMS has no CMake config; see cmake/FindLCMS2.cmake.
find_package(LCMS2 2.12 REQUIRED)

# Vulkan canvas candidate and the experimental GPU compositor (D-11, D-12). Shaders compile
# to SPIR-V at build time with glslangValidator.
find_package(Vulkan 1.3 REQUIRED COMPONENTS glslangValidator)

# Command envelopes, CLI parsing and logging (D-07).
find_package(nlohmann_json 3.11 REQUIRED)
find_package(CLI11 2.4 REQUIRED)
find_package(fmt 10 REQUIRED)
find_package(spdlog 1.12 REQUIRED)

# Unit and integration tests (D-05).
find_package(GTest 1.14 REQUIRED)

# Found now, linked by later sub-milestones: XXH3 cache keys (D-19, S1-2) and the M0 probes
# for text shaping and RAW decoding (S1-6). HarfBuzz's CMake config has no version file and
# FreeType and LibRaw ship none, so pkg-config supplies their imported targets. libraw_r is
# LibRaw's thread-safe build, matching the threaded job model (D-24).
find_package(PkgConfig REQUIRED)
pkg_check_modules(xxHash REQUIRED IMPORTED_TARGET libxxhash>=0.8)
pkg_check_modules(HarfBuzz REQUIRED IMPORTED_TARGET harfbuzz>=8.0)
pkg_check_modules(FreeType2 REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(LibRaw REQUIRED IMPORTED_TARGET libraw_r>=0.21)
