# Evaluation graph, scheduler, CPU kernels, GPU backend contracts (spec §5.5).
# S1-1 adds ViewTransform; S1-2 the graph and CPU evaluator.
archmage_add_module(render
    SOURCES
        src/ViewTransform.cpp
    PUBLIC_LINKS
        archmage_tiles)
