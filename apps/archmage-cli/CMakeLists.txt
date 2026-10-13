# Headless render, conversion, validation, batch execution (spec §5.5, App. A.4).
# Qt-free (D-07): links the core and io aggregates only.
add_executable(archmage-cli main.cpp Common.cpp Recover.cpp SelfTest.cpp)
target_link_libraries(archmage-cli PRIVATE archmage_core archmage_io CLI11::CLI11)
archmage_target_warnings(archmage-cli)
