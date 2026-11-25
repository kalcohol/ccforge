# Loaded by forge.cmake only for a full configuration, after forge::std.

function(_forge_define_tristate_option option_name default_value description)
    set(effective_value "${default_value}")
    if(DEFINED ${option_name})
        set(effective_value "${${option_name}}")
    endif()
    if(NOT effective_value STREQUAL "ON"
            AND NOT effective_value STREQUAL "OFF"
            AND NOT effective_value STREQUAL "AUTO")
        message(FATAL_ERROR "${option_name} must be one of ON, OFF, or AUTO")
    endif()
    # A directory-local override must not change the shared cache default.
    set(${option_name} "${default_value}" CACHE STRING "${description}")
    set_property(CACHE ${option_name} PROPERTY STRINGS ON OFF AUTO)
endfunction()

option(FORGE_ENABLE_FORGE_RUNTIME "Enable forge:: runtime utility targets" ON)
option(FORGE_ENABLE_FORGE_RESOURCE_POLICY "Enable forge:: resource policy facilities" ON)
_forge_define_tristate_option(FORGE_ENABLE_FORGE_IO AUTO "Enable forge:: IO backends when available")
_forge_define_tristate_option(FORGE_ENABLE_FORGE_IO_URING AUTO "Enable the Linux io_uring backend when available")

# Repeat the declarations on matching inclusions, but keep the existing
# probes and target configuration.
if(_forge_already_configured)
    return()
endif()

_forge_compute_probe_fingerprint(
    _forge_runtime_probe_fingerprint "${CMAKE_CXX_STANDARD}")
_forge_refresh_probe_cache(
    FORGE_RUNTIME_PROBE_FINGERPRINT "${_forge_runtime_probe_fingerprint}"
    FORGE_PROBE_LINUX_EPOLL_EVENTFD
    FORGE_PROBE_LINUX_IO_URING
    FORGE_PROBE_WINDOWS_IOCP)

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

if(NOT "${FORGE_ENABLE_FORGE_IO_URING}" STREQUAL "OFF" AND
   NOT "${FORGE_ENABLE_FORGE_IO}" STREQUAL "OFF")
    check_cxx_source_compiles("
        #if !defined(__linux__)
        #error Linux only
        #endif
        #include <linux/io_uring.h>
        #include <sys/mman.h>
        #include <sys/syscall.h>
        #include <unistd.h>
        #if !defined(__NR_io_uring_setup)
        #error io_uring_setup syscall number unavailable
        #endif
        #if !defined(__NR_io_uring_enter)
        #error io_uring_enter syscall number unavailable
        #endif
        #if !defined(__NR_io_uring_register)
        #error io_uring_register syscall number unavailable
        #endif
        int main() {
            io_uring_params params{};
            io_uring_sqe sqe{};
            io_uring_cqe cqe{};
            const unsigned required_operations[] = {
                IORING_OP_NOP,
                IORING_OP_READ,
                IORING_OP_WRITE,
                IORING_OP_ASYNC_CANCEL};
            const unsigned required_registration = IORING_REGISTER_PROBE;
            const unsigned required_feature = IORING_FEAT_NODROP;
            const long required_syscalls[] = {
                __NR_io_uring_setup,
                __NR_io_uring_enter,
                __NR_io_uring_register};
            (void)params;
            (void)sqe;
            (void)cqe;
            (void)required_operations;
            (void)required_registration;
            (void)required_feature;
            (void)required_syscalls;
            (void)sizeof(io_uring_probe);
            return 0;
        }
    " FORGE_PROBE_LINUX_IO_URING)
else()
    set(FORGE_PROBE_LINUX_IO_URING OFF)
endif()

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

set(FORGE_HAS_FORGE_IO_URING_BACKEND OFF)
if("${FORGE_ENABLE_FORGE_IO}" STREQUAL "OFF")
    # The io_uring proof backend is part of forge::io: disabling the parent
    # gate must disable it too, or the FORGE_ENABLE_FORGE_IO=OFF audit
    # (zero tests matching forge_io|example_forge_io) breaks.
    if("${FORGE_ENABLE_FORGE_IO_URING}" STREQUAL "ON")
        message(FATAL_ERROR
            "FORGE_ENABLE_FORGE_IO_URING=ON conflicts with FORGE_ENABLE_FORGE_IO=OFF")
    endif()
    message(STATUS "CC Forge: forge::io io_uring backend disabled (forge::io disabled)")
elseif("${FORGE_ENABLE_FORGE_IO_URING}" STREQUAL "OFF")
    message(STATUS "CC Forge: forge::io io_uring backend disabled")
elseif(FORGE_PROBE_LINUX_IO_URING)
    set(FORGE_HAS_FORGE_IO_URING_BACKEND ON)
    message(STATUS "CC Forge: forge::io io_uring backend enabled")
elseif("${FORGE_ENABLE_FORGE_IO_URING}" STREQUAL "ON")
    message(FATAL_ERROR
        "FORGE_ENABLE_FORGE_IO_URING=ON requires Linux io_uring UAPI support")
else()
    message(STATUS "CC Forge: forge::io io_uring backend unavailable - skipped")
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
    endif()
    if(TARGET forge::forge)
        get_target_property(
            _forge_source_wrapper_owner forge::forge _CCFORGE_SOURCE_WRAPPER)
        if(NOT _forge_source_wrapper_owner STREQUAL "forge")
            message(FATAL_ERROR
                "CC Forge cannot create forge::forge because that target already exists")
        endif()
    else()
        add_library(forge::forge INTERFACE IMPORTED GLOBAL)
        set_property(TARGET forge::forge PROPERTY _CCFORGE_SOURCE_WRAPPER forge)
        set_property(TARGET forge::forge PROPERTY
            INTERFACE_LINK_LIBRARIES forge)
    endif()
    unset(_forge_source_wrapper_owner)
endif()

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
_forge_apply_language_guard(${_FORGE_FULL_TARGET})

if(FORGE_HAS_FORGE_IO_BACKEND)
    target_compile_definitions(${_FORGE_FULL_TARGET} INTERFACE FORGE_HAS_FORGE_IO_BACKEND=1)
endif()
if(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)
    target_compile_definitions(${_FORGE_FULL_TARGET} INTERFACE FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND=1)
endif()
if(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND)
    target_compile_definitions(${_FORGE_FULL_TARGET} INTERFACE FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND=1)
endif()
if(FORGE_HAS_FORGE_IO_URING_BACKEND)
    target_compile_definitions(${_FORGE_FULL_TARGET} INTERFACE FORGE_HAS_FORGE_IO_URING_BACKEND=1)
endif()
message(STATUS "CC Forge library configured: ${FORGE_INCLUDE_DIR}")
