# Locate the standard cJSON development headers and library.
find_path(cJSON_INCLUDE_DIR NAMES cJSON.h PATH_SUFFIXES cjson)
find_library(cJSON_LIBRARY NAMES cjson)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(cJSON REQUIRED_VARS cJSON_INCLUDE_DIR cJSON_LIBRARY)

if(cJSON_FOUND AND NOT TARGET cJSON::cJSON)
    add_library(cJSON::cJSON UNKNOWN IMPORTED)
    set_target_properties(cJSON::cJSON PROPERTIES
        IMPORTED_LOCATION "${cJSON_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${cJSON_INCLUDE_DIR}")
    if(UNIX)
        set_property(TARGET cJSON::cJSON PROPERTY INTERFACE_LINK_LIBRARIES m)
    endif()
endif()
mark_as_advanced(cJSON_INCLUDE_DIR cJSON_LIBRARY)
