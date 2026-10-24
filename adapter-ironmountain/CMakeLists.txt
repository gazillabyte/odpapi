cmake_minimum_required(VERSION 3.21)

project(IronMountainSecureSyncAdapter VERSION 0.1.0 LANGUAGES C)

set(ODPAPI_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH
    "Path to the ODPAPI source tree")
option(IRONMOUNTAIN_BUILD_APACHE "Build mod_ironmountain when Apache headers exist" ON)
find_path(DB_INCLUDE_DIR db.h)
find_library(DB_LIBRARY NAMES db-5.3 db-5.2 db-5 db)
find_path(LIBSSH_INCLUDE_DIR libssh/libssh.h)
find_library(LIBSSH_LIBRARY NAMES ssh libssh)
find_path(LIBCSV_INCLUDE_DIR csv.h)
find_library(LIBCSV_LIBRARY NAMES csv libcsv)
if(NOT DB_INCLUDE_DIR OR NOT DB_LIBRARY OR NOT LIBSSH_INCLUDE_DIR OR NOT LIBSSH_LIBRARY OR
   NOT LIBCSV_INCLUDE_DIR OR NOT LIBCSV_LIBRARY)
    message(FATAL_ERROR "Berkeley DB, libssh, and libcsv development files are required")
endif()

add_subdirectory("${ODPAPI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/odpapi")
set_target_properties(odpapi PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(ironmountain_core STATIC src/irm_report.c src/irm_database.c)
set_target_properties(ironmountain_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(ironmountain_core PUBLIC c_std_99)
target_include_directories(ironmountain_core PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include" "${DB_INCLUDE_DIR}" "${LIBCSV_INCLUDE_DIR}")
target_link_libraries(ironmountain_core PUBLIC "${DB_LIBRARY}" "${LIBCSV_LIBRARY}")
target_compile_options(ironmountain_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ironmountain-sync src/ironmountain-sync.c)
target_compile_features(ironmountain-sync PRIVATE c_std_99)
target_include_directories(ironmountain-sync PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/include" "${LIBSSH_INCLUDE_DIR}")
target_link_libraries(ironmountain-sync PRIVATE ironmountain_core "${LIBSSH_LIBRARY}")
target_compile_options(ironmountain-sync PRIVATE -Wall -Wextra -Wpedantic)

if(IRONMOUNTAIN_BUILD_APACHE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(APR QUIET IMPORTED_TARGET apr-1)
    endif()
    find_path(APACHE_INCLUDE_DIR httpd.h PATH_SUFFIXES apache2)
    find_path(APACHE_MODULES_INCLUDE_DIR ap_config.h PATH_SUFFIXES apache2)
endif()

if(IRONMOUNTAIN_BUILD_APACHE AND TARGET PkgConfig::APR AND APACHE_INCLUDE_DIR AND APACHE_MODULES_INCLUDE_DIR)
add_library(mod_ironmountain MODULE src/mod_ironmountain.c)
target_compile_features(mod_ironmountain PRIVATE c_std_99)
target_include_directories(mod_ironmountain PRIVATE
    "${APACHE_INCLUDE_DIR}"
    "${APACHE_MODULES_INCLUDE_DIR}"
    "${ODPAPI_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${DB_INCLUDE_DIR}")
target_link_libraries(mod_ironmountain PRIVATE ODPAPI::odpapi ironmountain_core PkgConfig::APR)
set_target_properties(mod_ironmountain PROPERTIES
    PREFIX ""
    OUTPUT_NAME "mod_ironmountain")

install(TARGETS mod_ironmountain LIBRARY DESTINATION lib)
else()
    message(WARNING "Apache development files not found; mod_ironmountain will not be built")
endif()

install(TARGETS ironmountain-sync RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(ironmountain-test test/test_irm.c)
    target_compile_features(ironmountain-test PRIVATE c_std_99)
    target_include_directories(ironmountain-test PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/include")
    target_link_libraries(ironmountain-test PRIVATE ironmountain_core)
    add_test(NAME ironmountain-parser-database COMMAND ironmountain-test)
endif()
