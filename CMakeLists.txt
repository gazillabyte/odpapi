cmake_minimum_required(VERSION 3.21)

project(ODPAPI
    VERSION 1.0.0
    DESCRIPTION "Open Data Protection API C helper library"
    LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ODPAPI_BUILD_DOCS "Enable the Doxygen documentation target when available" ON)
option(ODPAPI_USE_SYSTEM_CJSON "Prefer an installed cJSON library" ON)
option(ODPAPI_FETCH_CJSON "Download cJSON when no installed library is selected" ON)

if(ODPAPI_USE_SYSTEM_CJSON)
    find_package(cJSON QUIET MODULE)
endif()
if(NOT TARGET cJSON::cJSON)
    if(NOT ODPAPI_FETCH_CJSON)
        message(FATAL_ERROR "Install the cJSON development package or enable ODPAPI_FETCH_CJSON")
    endif()
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(ENABLE_CJSON_TEST OFF CACHE BOOL "Build cJSON tests" FORCE)
    set(ENABLE_CJSON_UTILS OFF CACHE BOOL "Build cJSON utilities" FORCE)
    set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "Build cJSON uninstall target" FORCE)
    set(ENABLE_CUSTOM_COMPILER_FLAGS OFF CACHE BOOL "Use cJSON compiler flags" FORCE)
    set(CJSON_OVERRIDE_BUILD_SHARED_LIBS ON CACHE BOOL "Select cJSON library type" FORCE)
    set(CJSON_BUILD_SHARED_LIBS ON CACHE BOOL "Build shared cJSON" FORCE)
    # cJSON 1.7.19 declares an older policy version. Use a supported policy
    # floor on CMake 4 without modifying the upstream source.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND
       (NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM OR
        CMAKE_POLICY_VERSION_MINIMUM VERSION_LESS 3.10))
        set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
    endif()
    FetchContent_Declare(cjson
        URL https://codeload.github.com/DaveGamble/cJSON/tar.gz/refs/tags/v1.7.19
        URL_HASH SHA256=7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562
        TLS_VERIFY TRUE
        TIMEOUT 60)
    FetchContent_MakeAvailable(cjson)
    install(FILES "${cjson_SOURCE_DIR}/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/cJSON)
endif()

add_library(odpapi STATIC src/odpapi.c)
add_library(ODPAPI::odpapi ALIAS odpapi)
target_compile_features(odpapi PUBLIC c_std_99)
if(TARGET cJSON::cJSON)
    target_link_libraries(odpapi PUBLIC cJSON::cJSON)
else()
    target_link_libraries(odpapi PUBLIC
        $<BUILD_INTERFACE:cjson>
        $<INSTALL_INTERFACE:cJSON::cJSON>)
    target_include_directories(odpapi PUBLIC $<BUILD_INTERFACE:${cjson_SOURCE_DIR}>)
endif()
target_include_directories(odpapi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(odpapi PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "include/odpapi.h")

if(MSVC)
    target_compile_definitions(odpapi PRIVATE WIN32)
    target_compile_options(odpapi PRIVATE /W4)
else()
    target_compile_options(odpapi PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS odpapi
    EXPORT ODPAPITargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ODPAPITargets
    FILE ODPAPITargets.cmake
    NAMESPACE ODPAPI::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ODPAPI)

configure_package_config_file(
    cmake/ODPAPIConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ODPAPIConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ODPAPI)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ODPAPIConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ODPAPIConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ODPAPIConfigVersion.cmake
    cmake/FindcJSON.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ODPAPI)
install(FILES LICENSE NOTICE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/ODPAPI)

if(ODPAPI_BUILD_DOCS)
    find_package(Doxygen QUIET)
    if(Doxygen_FOUND)
        set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs)
        set(DOXYGEN_GENERATE_HTML YES)
        set(DOXYGEN_GENERATE_LATEX NO)
        set(DOXYGEN_WARN_AS_ERROR YES)
        set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
        set(DOXYGEN_FILE_PATTERNS *.h *.c)
        set(DOXYGEN_RECURSIVE YES)
        set(DOXYGEN_EXTRACT_ALL YES)
        set(DOXYGEN_OUTPUT_LANGUAGE English)
        set(DOXYGEN_PROJECT_NAME "ODPAPI C Helpers")
        set(DOXYGEN_PROJECT_NUMBER ${PROJECT_VERSION})
        doxygen_add_docs(odpapi-docs ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src
            COMMENT "Generating ODPAPI API documentation")
    else()
        message(STATUS "Doxygen not found; the odpapi-docs target is unavailable")
    endif()
endif()

include(CTest)
