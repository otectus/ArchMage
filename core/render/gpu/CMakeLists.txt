# Headless Vulkan compute for the GPU evaluation slice (D-12, ADR-0012; spec §7.5; plan S1-6.1, S1-6.2).
# Qt-free. Not part of archmage_core: only code that opts into GPU evaluation links it.
archmage_add_module(gpu
    SOURCES
        src/GpuContext.cpp
        src/GpuEvaluator.cpp
    PUBLIC_LINKS
        archmage_render
    PRIVATE_LINKS
        Vulkan::Vulkan)
target_include_directories(archmage_gpu PRIVATE src)
archmage_add_spirv_headers(archmage_gpu SOURCES shaders/source_over.comp TARGET_ENV vulkan1.1)
# Debug builds run the Khronos validation layer unless ARCHMAGE_VK_VALIDATION=0.
target_compile_definitions(archmage_gpu PRIVATE $<$<CONFIG:Debug>:ARCHMAGE_GPU_VALIDATION_DEFAULT>)
