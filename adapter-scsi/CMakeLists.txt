cmake_minimum_required(VERSION 3.21)

project(ODPAPISCSIAdapter VERSION 0.1.0 LANGUAGES C)

set(ODPAPI_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH
    "Path to the ODPAPI source tree")

add_subdirectory("${ODPAPI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/odpapi")
set_target_properties(odpapi PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(scsi_adapter_core STATIC src/scsi_adapter.c)
set_target_properties(scsi_adapter_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(scsi_adapter_core PUBLIC c_std_99)
target_include_directories(scsi_adapter_core PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_options(scsi_adapter_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(scsi_adapter_core PUBLIC ODPAPI::odpapi)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(APR QUIET IMPORTED_TARGET apr-1)
endif()
find_path(APACHE_INCLUDE_DIR httpd.h PATH_SUFFIXES apache2 httpd)
find_path(APACHE_MODULES_INCLUDE_DIR ap_config.h PATH_SUFFIXES apache2 httpd)

if(TARGET PkgConfig::APR AND APACHE_INCLUDE_DIR AND APACHE_MODULES_INCLUDE_DIR)
	add_library(mod_odpapi_scsi MODULE src/odpapi_scsi.c src/scsi_probe.c)
    target_compile_features(mod_odpapi_scsi PRIVATE c_std_99)
    target_include_directories(mod_odpapi_scsi PRIVATE
        "${APACHE_INCLUDE_DIR}"
        "${APACHE_MODULES_INCLUDE_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/include")
    target_link_libraries(mod_odpapi_scsi PRIVATE scsi_adapter_core PkgConfig::APR)
    target_compile_options(mod_odpapi_scsi PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(mod_odpapi_scsi PROPERTIES PREFIX "" OUTPUT_NAME "mod_odpapi_scsi")
    install(TARGETS mod_odpapi_scsi LIBRARY DESTINATION lib)
else()
    message(WARNING "Apache development files not found; mod_odpapi_scsi will not be built")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(scsi-adapter-test test/test_scsi_adapter.c)
    target_compile_features(scsi-adapter-test PRIVATE c_std_99)
    target_include_directories(scsi-adapter-test PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/include")
    target_link_libraries(scsi-adapter-test PRIVATE scsi_adapter_core)
    add_test(NAME scsi-adapter-json COMMAND scsi-adapter-test)
endif()

