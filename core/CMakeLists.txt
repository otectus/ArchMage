# Qt-free engine modules (D-06, D-07, ADR-0102): no Qt target may be linked from core/.
add_subdirectory(base)
add_subdirectory(document)
add_subdirectory(commands)
add_subdirectory(tiles)
add_subdirectory(render)
add_subdirectory(color)
add_subdirectory(brush)

# D-06: one interface target aggregating the core modules.
add_library(archmage_core INTERFACE)
target_link_libraries(archmage_core INTERFACE
    archmage_base
    archmage_document
    archmage_commands
    archmage_tiles
    archmage_render
    archmage_color
    archmage_brush)
