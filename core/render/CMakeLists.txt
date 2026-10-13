# Evaluation graph, scheduler, CPU kernels, GPU backend contracts (spec §5.5). S1-1: ViewTransform;
# S1-2: kernels, graph nodes, compiler and the CPU evaluator; S1-6 adds the GPU comparison path.
archmage_add_module(render
    SOURCES
        src/ViewTransform.cpp
        src/Kernels.cpp
        src/Graph.cpp
        src/CpuEvaluator.cpp
    PUBLIC_LINKS
        archmage_base
        archmage_tiles
        archmage_color
        archmage_document
    PRIVATE_LINKS
        PkgConfig::xxHash)
