# Operations, validation, transactions, undo descriptions (spec §5.5; D-15, D-24).
archmage_add_module(commands
    SOURCES
        src/Command.cpp
        src/LayerCommands.cpp
        src/MutationExecutor.cpp
        src/JobScheduler.cpp
    PUBLIC_LINKS
        archmage_base
        archmage_document
        nlohmann_json::nlohmann_json)
