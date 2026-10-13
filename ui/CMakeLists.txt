# Tools, panels, canvas interaction, accessibility, themes (spec §5.5). The only module that
# links Qt; core/ and io/ stay Qt-free (D-07).
archmage_add_module(ui
    SOURCES
        include/archmage/ui/MainWindow.h
        src/MainWindow.cpp
        include/archmage/ui/DocumentView.h
        src/DocumentView.cpp
        include/archmage/ui/document/DocumentSession.h
        src/document/DocumentSession.cpp
        include/archmage/ui/ColorState.h
        src/ColorState.cpp
        include/archmage/ui/OptionsRow.h
        src/OptionsRow.cpp
        src/ShellPrompts.cpp
        src/Theme.cpp
        include/archmage/ui/panels/LayersModel.h
        src/panels/LayersModel.cpp
        include/archmage/ui/panels/LayersPanel.h
        src/panels/LayersPanel.cpp
        include/archmage/ui/panels/ColorPanel.h
        src/panels/ColorPanel.cpp
        src/canvas/DocumentFrameSource.cpp
        src/tools/BrushTool.cpp
        src/tools/NavigationTools.cpp
        include/archmage/ui/canvas/CanvasSurface.h
        src/canvas/CanvasSurface.cpp
        include/archmage/ui/canvas/RasterCanvasSurface.h
        src/canvas/RasterCanvasSurface.cpp
        include/archmage/ui/canvas/RhiCanvasSurface.h
        src/canvas/RhiCanvasSurface.cpp
        include/archmage/ui/canvas/VulkanCanvasSurface.h
        src/canvas/VulkanCanvasSurface.cpp
        src/canvas/VulkanSupport.cpp
        src/canvas/SyntheticFrameSource.cpp
        src/canvas/LatencyTrace.cpp
        src/input/InputMapper.cpp
        src/experiments/CanvasExperiment.cpp
        src/experiments/BrushLatencyBenchmark.cpp
    PUBLIC_LINKS
        Qt6::Widgets
        archmage_core
        archmage_native
    PRIVATE_LINKS
        Qt6::GuiPrivate
        Vulkan::Headers)

set_target_properties(archmage_ui PROPERTIES AUTOMOC ON)

# Original tool and panel icons (ADR-0028, resources/icons). The SVG icon engine is a Qt plugin from
# qt6-svg, loaded at run time; nothing links Qt6::Svg.
file(GLOB ARCHMAGE_ICONS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/icons/*.svg")
qt_add_resources(archmage_ui "archmage_icons"
    PREFIX "/archmage/icons"
    BASE "${PROJECT_SOURCE_DIR}/resources/icons"
    FILES ${ARCHMAGE_ICONS})

# Canvas presentation shaders (S1-1, D-11): SPIR-V headers for the QVulkanWindow surface and qsb packages
# for the QRhiWidget surface, all compiled at build time (D-12).
archmage_add_spirv_headers(archmage_ui SOURCES shaders/present.vert shaders/present.frag)
qt_add_shaders(archmage_ui "archmage_canvas_shaders"
    PREFIX "/archmage"
    FILES
        shaders/present_rhi.vert
        shaders/present.frag)
