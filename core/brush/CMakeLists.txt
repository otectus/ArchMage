# Input processing, brush engines, stroke replay and deltas (spec §5.5). S1-3: the round brush.
archmage_add_module(brush
    SOURCES
        src/Stroke.cpp
    PUBLIC_LINKS
        archmage_base
        archmage_tiles
        archmage_document
        nlohmann_json::nlohmann_json)
