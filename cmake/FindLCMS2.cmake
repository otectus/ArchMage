# FindLCMS2
# ---------
# Little CMS 2 (D-10). Little CMS installs no CMake package config, so this module asks
# pkg-config first and falls back to a plain header and library search.
#
# Result variables: LCMS2_FOUND, LCMS2_VERSION (major.minor), LCMS2_INCLUDE_DIR, LCMS2_LIBRARY
# Imported target:  LCMS2::LCMS2

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_LCMS2 QUIET lcms2)
endif()

find_path(LCMS2_INCLUDE_DIR NAMES lcms2.h HINTS ${PC_LCMS2_INCLUDE_DIRS})
find_library(LCMS2_LIBRARY NAMES lcms2 liblcms2 HINTS ${PC_LCMS2_LIBRARY_DIRS})

if(PC_LCMS2_VERSION)
    set(LCMS2_VERSION "${PC_LCMS2_VERSION}")
elseif(LCMS2_INCLUDE_DIR AND EXISTS "${LCMS2_INCLUDE_DIR}/lcms2.h")
    # lcms2.h encodes the version as major * 1000 + minor * 10, e.g. 2190 for 2.19.
    file(STRINGS "${LCMS2_INCLUDE_DIR}/lcms2.h" lcms2_version_line REGEX "^#define[ \t]+LCMS_VERSION[ \t]+[0-9]+")
    string(REGEX REPLACE "^#define[ \t]+LCMS_VERSION[ \t]+([0-9]+).*$" "\\1" lcms2_version_number "${lcms2_version_line}")
    if(lcms2_version_number)
        math(EXPR lcms2_major "${lcms2_version_number} / 1000")
        math(EXPR lcms2_minor "(${lcms2_version_number} % 1000) / 10")
        set(LCMS2_VERSION "${lcms2_major}.${lcms2_minor}")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LCMS2
    REQUIRED_VARS LCMS2_LIBRARY LCMS2_INCLUDE_DIR
    VERSION_VAR LCMS2_VERSION)

if(LCMS2_FOUND AND NOT TARGET LCMS2::LCMS2)
    add_library(LCMS2::LCMS2 UNKNOWN IMPORTED)
    set_target_properties(LCMS2::LCMS2 PROPERTIES
        IMPORTED_LOCATION "${LCMS2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LCMS2_INCLUDE_DIR}")
endif()

mark_as_advanced(LCMS2_INCLUDE_DIR LCMS2_LIBRARY)
