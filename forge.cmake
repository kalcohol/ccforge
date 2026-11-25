# CC Forge CMake Configuration
# Include this file in your project to use CC Forge library

cmake_minimum_required(VERSION 3.20)

# Get the directory where this file is located. Installed package configs set
# the _CCFORGE_PACKAGE_* variables before including this file so probes still
# run in the consumer project while paths point at the install tree.
get_filename_component(_FORGE_CURRENT_LIST_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)

if(DEFINED _CCFORGE_PACKAGE_PREFIX)
    set(FORGE_ROOT_DIR "${_CCFORGE_PACKAGE_PREFIX}")
elseif(NOT DEFINED FORGE_ROOT_DIR)
    set(FORGE_ROOT_DIR "${_FORGE_CURRENT_LIST_DIR}")
endif()

if(DEFINED _CCFORGE_PACKAGE_INCLUDE_DIR)
    set(FORGE_INCLUDE_DIR "${_CCFORGE_PACKAGE_INCLUDE_DIR}")
elseif(NOT DEFINED FORGE_INCLUDE_DIR)
    set(FORGE_INCLUDE_DIR "${FORGE_ROOT_DIR}/include")
endif()

if(DEFINED _CCFORGE_PACKAGE_BACKPORT_DIR)
    set(FORGE_BACKPORT_DIR "${_CCFORGE_PACKAGE_BACKPORT_DIR}")
elseif(NOT DEFINED FORGE_BACKPORT_DIR)
    set(FORGE_BACKPORT_DIR "${FORGE_ROOT_DIR}/backport")
endif()

if(DEFINED _CCFORGE_PACKAGE_CMAKE_DIR)
    set(FORGE_CMAKE_DIR "${_CCFORGE_PACKAGE_CMAKE_DIR}")
elseif(NOT DEFINED FORGE_CMAKE_DIR)
    set(FORGE_CMAKE_DIR "${FORGE_ROOT_DIR}/cmake")
endif()

# A source tree or prefix with no extension headers defaults to standard-only.
if(IS_DIRECTORY "${FORGE_INCLUDE_DIR}/forge")
    set(_forge_extensions_default ON)
else()
    set(_forge_extensions_default OFF)
endif()
option(FORGE_ENABLE_EXTENSIONS "Enable the forge::forge extension target" ${_forge_extensions_default})
unset(_forge_extensions_default)

# Normalize force defaults before comparing repeated inclusions.
foreach(_forge_feature SIMD SENDERS CONSTANT_WRAPPER MDSPAN_PADDED_LAYOUTS SUBMDSPAN LINALG HIVE)
    option(FORGE_FORCE_${_forge_feature}_BACKPORT
        "Force the ${_forge_feature} backport even if native support exists" OFF)
endforeach()

if(NOT DEFINED CMAKE_CXX_STANDARD OR "${CMAKE_CXX_STANDARD}" STREQUAL "")
    message(FATAL_ERROR
        "CC Forge requires an explicit CMAKE_CXX_STANDARD (23 or newer) before inclusion; target_compile_features alone cannot select the probe language mode.")
endif()

include("${FORGE_CMAKE_DIR}/ForgeConfiguration.cmake")
_forge_check_configuration(_forge_already_configured)
if(_forge_already_configured)
    if(FORGE_ENABLE_EXTENSIONS)
        include("${FORGE_CMAKE_DIR}/ForgeExtensions.cmake")
    endif()
    get_property(_forge_outputs GLOBAL PROPERTY _FORGE_CONFIGURATION_OUTPUTS)
    foreach(_forge_output IN LISTS _forge_outputs)
        get_property(${_forge_output} GLOBAL PROPERTY _FORGE_OUTPUT_${_forge_output})
    endforeach()
    return()
endif()

if(FORGE_ENABLE_EXTENSIONS AND NOT IS_DIRECTORY "${FORGE_INCLUDE_DIR}/forge")
    message(FATAL_ERROR
        "FORGE_ENABLE_EXTENSIONS=ON requires the CC Forge extension headers at ${FORGE_INCLUDE_DIR}/forge. Use a full source tree or installation, or set FORGE_ENABLE_EXTENSIONS=OFF.")
endif()

include(CheckCXXSourceCompiles)
include("${FORGE_CMAKE_DIR}/ForgeProbeFingerprint.cmake")

find_package(Threads REQUIRED)

if(DEFINED _CCFORGE_PACKAGE_PREFIX)
    set(_FORGE_PACKAGE_MODE ON)
else()
    set(_FORGE_PACKAGE_MODE OFF)
endif()

if(DEFINED CMAKE_CXX_STANDARD AND CMAKE_CXX_STANDARD LESS 23)
    message(FATAL_ERROR "CC Forge requires C++23 or later. Please set CMAKE_CXX_STANDARD to 23 or newer.")
endif()

# Create the standard-header target first. It exposes only standard-shaped
# headers such as <execution> and <simd>, using native stand-aside or Forge
# backport injection according to the probes below. Installed package configs
# create imported namespace targets directly so downstream install(EXPORT)
# graphs do not depend on local implementation target names.
if(_FORGE_PACKAGE_MODE)
    set(_FORGE_STD_TARGET forge::std)
    if(NOT TARGET forge::std)
        add_library(forge::std INTERFACE IMPORTED GLOBAL)
    endif()
else()
    set(_FORGE_STD_TARGET forge_std)
    if(NOT TARGET forge_std)
        add_library(forge_std INTERFACE)
    endif()
    if(TARGET forge::std)
        get_target_property(
            _forge_source_wrapper_owner forge::std _CCFORGE_SOURCE_WRAPPER)
        if(NOT _forge_source_wrapper_owner STREQUAL "std")
            message(FATAL_ERROR
                "CC Forge cannot create forge::std because that target already exists")
        endif()
    else()
        add_library(forge::std INTERFACE IMPORTED GLOBAL)
        set_property(TARGET forge::std PROPERTY _CCFORGE_SOURCE_WRAPPER std)
        set_property(TARGET forge::std PROPERTY
            INTERFACE_LINK_LIBRARIES forge_std)
    endif()
    unset(_forge_source_wrapper_owner)
endif()

target_compile_features(${_FORGE_STD_TARGET} INTERFACE cxx_std_23)

target_compile_options(${_FORGE_STD_TARGET} INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
)

target_link_libraries(${_FORGE_STD_TARGET} INTERFACE Threads::Threads)

set(FORGE_BACKPORT_TARGET ${_FORGE_STD_TARGET})
include("${FORGE_CMAKE_DIR}/ForgeBackportProbes.cmake")
unset(FORGE_BACKPORT_TARGET)

# The shadowing backport root requires the real standard-header paths on MSVC.
if(FORGE_NEEDS_BACKPORT)
    if(MSVC)
        include("${FORGE_CMAKE_DIR}/ForgeMsvcHeaders.cmake")
        _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_UTILITY_HEADER utility TRUE)
        _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_SIMD_HEADER simd FALSE)
        _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_EXECUTION_HEADER execution TRUE)
        _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_MDSPAN_HEADER mdspan FALSE)
        _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_LINALG_HEADER linalg FALSE)
        _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_HIVE_HEADER hive FALSE)
    endif()

    if(_FORGE_PACKAGE_MODE)
        target_include_directories(${_FORGE_STD_TARGET} BEFORE INTERFACE
            "${FORGE_BACKPORT_DIR}")
    else()
        target_include_directories(${_FORGE_STD_TARGET} BEFORE INTERFACE
            $<BUILD_INTERFACE:${FORGE_BACKPORT_DIR}>)
    endif()
endif()

message(STATUS "CC Forge standard-header target configured")

_forge_add_language_guard(${_FORGE_STD_TARGET})
if(FORGE_ENABLE_EXTENSIONS)
    include("${FORGE_CMAKE_DIR}/ForgeExtensions.cmake")
else()
    set(FORGE_HAS_FORGE_IO_BACKEND OFF)
    set(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND OFF)
    set(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND OFF)
    set(FORGE_HAS_FORGE_IO_URING_BACKEND OFF)
endif()

# Publish derived decisions for identical inclusions from sibling directories.
get_cmake_property(_forge_variables VARIABLES)
set(_forge_outputs FORGE_PROBE_CXX_STANDARD)
foreach(_forge_variable IN LISTS _forge_variables)
    if(_forge_variable MATCHES "^FORGE_(HAS_|NEEDS_|.*_PARTIAL$)")
        list(APPEND _forge_outputs "${_forge_variable}")
    endif()
endforeach()
set_property(GLOBAL PROPERTY _FORGE_CONFIGURATION_OUTPUTS "${_forge_outputs}")
foreach(_forge_output IN LISTS _forge_outputs)
    set_property(GLOBAL PROPERTY _FORGE_OUTPUT_${_forge_output} "${${_forge_output}}")
endforeach()
