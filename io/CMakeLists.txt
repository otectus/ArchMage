# Persistence and interchange modules (spec §5.5); Qt-free like core/ (D-07).
add_subdirectory(native)
add_subdirectory(formats)

# Aggregate target for the io modules, mirroring archmage_core.
add_library(archmage_io INTERFACE)
target_link_libraries(archmage_io INTERFACE archmage_native archmage_formats)
