# Desktop executable: startup, sessions, workspace composition (spec §5.5).
add_executable(archmage main.cpp)
target_link_libraries(archmage PRIVATE archmage_ui)
archmage_target_warnings(archmage)
