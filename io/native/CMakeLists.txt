# Native .archmage container, save transaction and migrations (spec §10; D-13). The recovery workspace
# joins in S1-5.

# The schema DDL is embedded so that the binary and the schema cannot drift apart.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/schema/0001.sql" ARCHMAGE_SCHEMA_SQL)
configure_file(src/Format.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/Format.cpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/schema/0001.sql")

archmage_add_module(native
    SOURCES
        src/Blob.cpp
        src/Migration.cpp
        src/NativeReader.cpp
        src/NativeWriter.cpp
        src/Payloads.cpp
        src/Platform.cpp
        src/SaveTransaction.cpp
        src/Sqlite.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/Format.cpp"
    PUBLIC_LINKS archmage_core nlohmann_json::nlohmann_json
    PRIVATE_LINKS SQLite3::SQLite3 zstd::libzstd_shared)
target_include_directories(archmage_native PRIVATE src)
