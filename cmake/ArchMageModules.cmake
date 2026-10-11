# D-06 module targets.
#
# archmage_add_module(<name> [SOURCES <file>...] [PUBLIC_LINKS <target>...] [PRIVATE_LINKS <target>...])
#
# Creates archmage_<name> for a module directory laid out as
#   include/archmage/<name>/   public headers, namespace archmage::<name>
#   src/                       implementation
# A module with sources is a static library. A module without sources stays an INTERFACE
# library until its first source file lands; consumers link it the same way in both cases.
# List public headers that declare Q_OBJECT classes in SOURCES so AUTOMOC finds them.
function(archmage_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;PUBLIC_LINKS;PRIVATE_LINKS")
    set(target archmage_${name})

    if(ARG_SOURCES)
        add_library(${target} STATIC ${ARG_SOURCES})
        target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(${target} PUBLIC ${ARG_PUBLIC_LINKS} PRIVATE ${ARG_PRIVATE_LINKS})
        archmage_target_warnings(${target})
    else()
        if(ARG_PRIVATE_LINKS)
            message(FATAL_ERROR "${target}: PRIVATE_LINKS needs SOURCES")
        endif()
        add_library(${target} INTERFACE)
        target_include_directories(${target} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(${target} INTERFACE ${ARG_PUBLIC_LINKS})
    endif()
endfunction()
