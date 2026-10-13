# Sparse tile storage, sharing, cache, storage-backed eviction (spec §5.5; D-08, D-26).
archmage_add_module(tiles
    SOURCES
        src/SampleType.cpp
        src/ChannelLayout.cpp
        src/Tile.cpp
        src/TileMap.cpp
        src/ResourceCoordinator.cpp
        src/TileSpool.cpp
    PUBLIC_LINKS
        archmage_base)
