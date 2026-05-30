# CC Forge CMake Configuration
# Include this file in your project to use CC Forge library

cmake_minimum_required(VERSION 3.17)

# Get the directory where this file is located
get_filename_component(FORGE_ROOT_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)

# Set CC Forge include directory
set(FORGE_INCLUDE_DIR "${FORGE_ROOT_DIR}/include")
set(FORGE_BACKPORT_DIR "${FORGE_ROOT_DIR}/backport")
set(FORGE_CMAKE_DIR "${FORGE_ROOT_DIR}/cmake")

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
_forge_define_tristate_option(FORGE_ENABLE_FORGE_ACCEL AUTO "Enable forge:: accelerator backends when available")
option(FORGE_ENABLE_FORGE_TYPED_ERASURE "Enable future forge:: typed-error erasure facilities" OFF)

include(CheckCXXSourceCompiles)

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

set(FORGE_HAS_FORGE_IO_BACKEND OFF)
if("${FORGE_ENABLE_FORGE_IO}" STREQUAL "OFF")
    message(STATUS "CC Forge: forge::io backend disabled")
elseif(FORGE_PROBE_LINUX_EPOLL_EVENTFD)
    set(FORGE_HAS_FORGE_IO_BACKEND ON)
    message(STATUS "CC Forge: forge::io linux epoll backend enabled")
elseif("${FORGE_ENABLE_FORGE_IO}" STREQUAL "ON")
    message(FATAL_ERROR "FORGE_ENABLE_FORGE_IO=ON requires Linux epoll/eventfd support")
else()
    message(STATUS "CC Forge: forge::io backend unavailable - skipped")
endif()

set(FORGE_HAS_FORGE_ACCEL_BACKEND OFF)
if("${FORGE_ENABLE_FORGE_ACCEL}" STREQUAL "OFF")
    message(STATUS "CC Forge: forge::accel mock backend disabled")
elseif(FORGE_ENABLE_FORGE_RUNTIME AND FORGE_ENABLE_FORGE_RESOURCE_POLICY)
    set(FORGE_HAS_FORGE_ACCEL_BACKEND ON)
    message(STATUS "CC Forge: forge::accel mock backend enabled")
elseif("${FORGE_ENABLE_FORGE_ACCEL}" STREQUAL "ON")
    message(FATAL_ERROR "FORGE_ENABLE_FORGE_ACCEL=ON requires FORGE_ENABLE_FORGE_RUNTIME=ON and FORGE_ENABLE_FORGE_RESOURCE_POLICY=ON")
else()
    message(STATUS "CC Forge: forge::accel mock backend unavailable - skipped")
endif()

# Create INTERFACE library target for header-only library
if(NOT TARGET forge)
    add_library(forge INTERFACE)
    add_library(forge::forge ALIAS forge)

    # Set include directories
    target_include_directories(forge INTERFACE
        $<BUILD_INTERFACE:${FORGE_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
    )

    # MSVC: Enable UTF-8 source and execution charset (C++23 compliance)
    target_compile_options(forge INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    )

    if(FORGE_HAS_FORGE_IO_BACKEND)
        target_compile_definitions(forge INTERFACE FORGE_HAS_FORGE_IO_BACKEND=1)
    endif()
    if(FORGE_HAS_FORGE_ACCEL_BACKEND)
        target_compile_definitions(forge INTERFACE FORGE_HAS_FORGE_ACCEL_BACKEND=1)
    endif()

    include("${FORGE_CMAKE_DIR}/ForgeBackportProbes.cmake")

    # Add backport path if any feature needs it
    if(FORGE_NEEDS_BACKPORT)
        if(MSVC)
            include("${FORGE_CMAKE_DIR}/ForgeMsvcHeaders.cmake")
            _forge_define_msvc_standard_header(forge FORGE_MSVC_MEMORY_HEADER memory TRUE)
            _forge_define_msvc_standard_header(forge FORGE_MSVC_UTILITY_HEADER utility TRUE)
            _forge_define_msvc_standard_header(forge FORGE_MSVC_SIMD_HEADER simd FALSE)
            _forge_define_msvc_standard_header(forge FORGE_MSVC_EXECUTION_HEADER execution TRUE)
            _forge_define_msvc_standard_header(forge FORGE_MSVC_MDSPAN_HEADER mdspan FALSE)
        endif()

        target_include_directories(forge BEFORE INTERFACE
            $<BUILD_INTERFACE:${FORGE_BACKPORT_DIR}>
        )
    endif()

    # Add the backport root for experimental TS headers. Do not add
    # backport/experimental directly: that would make <memory> resolve to
    # backport/experimental/memory instead of the standard-header wrapper.
    if(FORGE_NEEDS_EXPERIMENTAL)
        target_include_directories(forge BEFORE INTERFACE
            $<BUILD_INTERFACE:${FORGE_BACKPORT_DIR}>
        )
    endif()

    message(STATUS "CC Forge library configured: ${FORGE_INCLUDE_DIR}")
endif()
