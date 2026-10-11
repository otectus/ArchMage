# Tools, panels, canvas interaction, accessibility, themes (spec §5.5). The only module that
# links Qt; core/ and io/ stay Qt-free (D-07).
archmage_add_module(ui
    SOURCES
        include/archmage/ui/MainWindow.h
        src/MainWindow.cpp
    PUBLIC_LINKS
        Qt6::Widgets
        archmage_core)

set_target_properties(archmage_ui PROPERTIES AUTOMOC ON)
