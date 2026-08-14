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

function(_forge_define_tristate_option option_name default_value description)
    set(${option_name} "${default_value}" CACHE STRING "${description}")
    set_property(CACHE ${option_name} PROPERTY STRINGS ON OFF AUTO)
    if(NOT "${${option_name}}" STREQUAL "ON"
            AND NOT "${${option_name}}" STREQUAL "OFF"
            AND NOT "${${option_name}}" STREQUAL "AUTO")
        message(FATAL_ERROR "${option_name} must be one of ON, OFF, or AUTO")
    endif()
endfunction()

option(FORGE_ENABLE_FORGE_RUNTIME "Enable forge:: runtime utility targets" ON)
option(FORGE_ENABLE_FORGE_RESOURCE_POLICY "Enable forge:: resource policy facilities" ON)
_forge_define_tristate_option(FORGE_ENABLE_FORGE_IO AUTO "Enable forge:: IO backends when available")

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

_forge_compute_probe_fingerprint(
    _forge_runtime_probe_fingerprint "${CMAKE_CXX_STANDARD}")
if(DEFINED FORGE_RUNTIME_PROBE_FINGERPRINT
        AND NOT "${FORGE_RUNTIME_PROBE_FINGERPRINT}" STREQUAL "${_forge_runtime_probe_fingerprint}")
    foreach(_forge_probe_var
            FORGE_PROBE_LINUX_EPOLL_EVENTFD
            FORGE_PROBE_WINDOWS_IOCP)
        unset(${_forge_probe_var} CACHE)
        unset(${_forge_probe_var})
    endforeach()
endif()
set(FORGE_RUNTIME_PROBE_FINGERPRINT "${_forge_runtime_probe_fingerprint}"
    CACHE INTERNAL "CC Forge runtime backend probe fingerprint")

check_cxx_source_compiles("
    #include <sys/epoll.h>
    #include <sys/eventfd.h>
    #include <unistd.h>
    int main() {
        int ep = epoll_create1(EPOLL_CLOEXEC);
        int ev = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (ep >= 0) close(ep);
        if (ev >= 0) close(ev);
        return 0;
    }
" FORGE_PROBE_LINUX_EPOLL_EVENTFD)

check_cxx_source_compiles("
    #ifndef _WIN32
    #error Windows only
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    int main() {
        HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
        if (port) CloseHandle(port);
        return port ? 0 : 1;
    }
" FORGE_PROBE_WINDOWS_IOCP)

set(FORGE_HAS_FORGE_IO_BACKEND OFF)
set(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND OFF)
set(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND OFF)
if("${FORGE_ENABLE_FORGE_IO}" STREQUAL "OFF")
    message(STATUS "CC Forge: forge::io backend disabled")
elseif(FORGE_PROBE_LINUX_EPOLL_EVENTFD)
    set(FORGE_HAS_FORGE_IO_BACKEND ON)
    set(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND ON)
    message(STATUS "CC Forge: forge::io linux epoll backend enabled")
elseif(FORGE_PROBE_WINDOWS_IOCP)
    set(FORGE_HAS_FORGE_IO_BACKEND ON)
    set(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND ON)
    message(STATUS "CC Forge: forge::io windows IOCP backend enabled")
elseif("${FORGE_ENABLE_FORGE_IO}" STREQUAL "ON")
    message(FATAL_ERROR "FORGE_ENABLE_FORGE_IO=ON requires Linux epoll/eventfd or Windows IOCP support")
else()
    message(STATUS "CC Forge: forge::io backend unavailable - skipped")
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
        add_library(forge::std ALIAS forge_std)
    endif()
endif()

if(NOT _FORGE_STD_TARGET_CONFIGURED)
    set(_FORGE_STD_TARGET_CONFIGURED ON)

    target_compile_features(${_FORGE_STD_TARGET} INTERFACE cxx_std_23)

    target_compile_options(${_FORGE_STD_TARGET} INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    )

    target_link_libraries(${_FORGE_STD_TARGET} INTERFACE Threads::Threads)

    set(FORGE_BACKPORT_TARGET ${_FORGE_STD_TARGET})
    include("${FORGE_CMAKE_DIR}/ForgeBackportProbes.cmake")
    unset(FORGE_BACKPORT_TARGET)

    # The shadowing backport root requires the real standard-header paths on
    # MSVC even when only an experimental TS header needs the root.
    if(FORGE_NEEDS_BACKPORT OR FORGE_NEEDS_EXPERIMENTAL)
        if(MSVC)
            include("${FORGE_CMAKE_DIR}/ForgeMsvcHeaders.cmake")
            _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_MEMORY_HEADER memory TRUE)
            _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_UTILITY_HEADER utility TRUE)
            _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_SIMD_HEADER simd FALSE)
            _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_EXECUTION_HEADER execution TRUE)
            _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_MDSPAN_HEADER mdspan FALSE)
            _forge_define_msvc_standard_header(${_FORGE_STD_TARGET} FORGE_MSVC_LINALG_HEADER linalg FALSE)
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
endif()

# Create the full extension target for include/forge utilities. Existing
# consumers keep linking forge::forge; std-only consumers can link forge::std.
if(_FORGE_PACKAGE_MODE)
    set(_FORGE_FULL_TARGET forge::forge)
    if(NOT TARGET forge::forge)
        add_library(forge::forge INTERFACE IMPORTED GLOBAL)
    endif()
else()
    set(_FORGE_FULL_TARGET forge)
    if(NOT TARGET forge)
        add_library(forge INTERFACE)
        add_library(forge::forge ALIAS forge)
    endif()
endif()

if(NOT _FORGE_FULL_TARGET_CONFIGURED)
    set(_FORGE_FULL_TARGET_CONFIGURED ON)

    if(_FORGE_PACKAGE_MODE)
        target_include_directories(${_FORGE_FULL_TARGET} INTERFACE
            "${FORGE_INCLUDE_DIR}")
    else()
        target_include_directories(${_FORGE_FULL_TARGET} INTERFACE
            $<BUILD_INTERFACE:${FORGE_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:include>
        )
    endif()

    target_link_libraries(${_FORGE_FULL_TARGET} INTERFACE forge::std)

    if(FORGE_HAS_FORGE_IO_BACKEND)
        target_compile_definitions(${_FORGE_FULL_TARGET} INTERFACE FORGE_HAS_FORGE_IO_BACKEND=1)
    endif()
    if(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)
        target_compile_definitions(${_FORGE_FULL_TARGET} INTERFACE FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND=1)
    endif()
    if(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND)
        target_compile_definitions(${_FORGE_FULL_TARGET} INTERFACE FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND=1)
    endif()
    message(STATUS "CC Forge library configured: ${FORGE_INCLUDE_DIR}")
endif()
