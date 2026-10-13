# GLSL to SPIR-V at build time (D-12): no shader compiler runs on an interaction-critical path.
#
# archmage_add_spirv_headers(<target> SOURCES <file.vert|.frag|.comp>... [TARGET_ENV vulkan1.0])
#
# Compiles each source with glslangValidator into a header `<file>.spv.h` declaring
#   const uint32_t kSpirv_<file_with_underscores>[]
# in the target's private generated include directory. Usable from Qt-free code.
function(archmage_add_spirv_headers target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "TARGET_ENV" "SOURCES")
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "archmage_add_spirv_headers(${target}): SOURCES is required")
    endif()
    if(NOT ARG_TARGET_ENV)
        set(ARG_TARGET_ENV vulkan1.0)
    endif()
    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/spirv")
    set(outputs "")
    foreach(source IN LISTS ARG_SOURCES)
        get_filename_component(name "${source}" NAME)
        string(MAKE_C_IDENTIFIER "${name}" identifier)
        set(output "${output_dir}/${name}.spv.h")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
            COMMAND Vulkan::glslangValidator --target-env ${ARG_TARGET_ENV} --quiet
                    --vn "kSpirv_${identifier}" -o "${output}" "${CMAKE_CURRENT_SOURCE_DIR}/${source}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${source}"
            COMMENT "Compiling ${source} to SPIR-V"
            VERBATIM)
        list(APPEND outputs "${output}")
    endforeach()
    target_sources(${target} PRIVATE ${outputs})
    target_include_directories(${target} PRIVATE "${output_dir}")
endfunction()
