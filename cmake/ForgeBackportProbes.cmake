# Backport feature probes for forge.cmake.
#
# This file expects the standard-header target to already exist. Set
# FORGE_BACKPORT_TARGET before including this file; it defaults to forge for
# compatibility with older direct include patterns. It sets:
#   FORGE_NEEDS_BACKPORT
#   FORGE_NEEDS_EXPERIMENTAL
# and publishes FORGE_HAS_NATIVE_* / FORGE_FORCE_* definitions on that target.

include(CheckCXXSourceCompiles)
include("${CMAKE_CURRENT_LIST_DIR}/ForgeProbeFingerprint.cmake")

set(FORGE_NEEDS_BACKPORT FALSE)

if(NOT DEFINED FORGE_BACKPORT_TARGET)
    set(FORGE_BACKPORT_TARGET forge)
endif()
if(NOT TARGET ${FORGE_BACKPORT_TARGET})
    message(FATAL_ERROR "CC Forge: FORGE_BACKPORT_TARGET '${FORGE_BACKPORT_TARGET}' does not exist")
endif()

# Probe at the SAME language standard the consumer compiles with. Detecting a
# C++26 feature that is only reachable under -std=c++26 is meaningless if the
# project itself builds at C++23, so native presence must be judged at the build
# standard, not the compiler max.
if(DEFINED CMAKE_CXX_STANDARD)
    set(_forge_std "${CMAKE_CXX_STANDARD}")
else()
    set(_forge_std 23)
endif()

set(_forge_modular_probe_sources
    "${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeSimd.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeExecution.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeConstantWrapper.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeMdspanPadded.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeSubmdspan.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeLinalg.cmake")
_forge_compute_probe_fingerprint(
    _forge_probe_fingerprint "${_forge_std}" ${_forge_modular_probe_sources})
_forge_refresh_probe_cache(
    FORGE_BACKPORT_PROBE_FINGERPRINT "${_forge_probe_fingerprint}"
    HAS_STD_UNIQUE_RESOURCE
    FORGE_SIMD_FULL
    FORGE_SIMD_PARTIAL_MACRO
    FORGE_SIMD_PARTIAL_VEC
    FORGE_SIMD_PARTIAL_MASK
    FORGE_SENDERS_FULL
    FORGE_SENDERS_PARTIAL_MACRO
    FORGE_SENDERS_PARTIAL_FACTORY
    FORGE_SENDERS_PARTIAL_SET_VALUE
    FORGE_SENDERS_PARTIAL_SET_ERROR
    FORGE_SENDERS_PARTIAL_SET_STOPPED
    FORGE_SENDERS_PARTIAL_CONNECT
    FORGE_SENDERS_PARTIAL_START
    FORGE_SENDERS_PARTIAL_GET_ENV
    FORGE_SENDERS_PARTIAL_AWAIT_ADAPTOR
    FORGE_SENDERS_PARTIAL_AS_AWAITABLE
    FORGE_SENDERS_PARTIAL_SCHEDULE_FROM
    FORGE_SENDERS_PARTIAL_TRANSFORM_SENDER
    FORGE_SENDERS_PARTIAL_APPLY_SENDER
    FORGE_SENDERS_PARTIAL_SENDER_MARKER
    FORGE_SENDERS_PARTIAL_SCHEDULER_MARKER
    FORGE_SENDERS_PARTIAL_RECEIVER_MARKER
    FORGE_SENDERS_PARTIAL_OPERATION_MARKER
    FORGE_SENDERS_PARTIAL
    FORGE_CONSTANT_WRAPPER_FULL
    FORGE_CONSTANT_WRAPPER_PARTIAL
    FORGE_BASE_MDSPAN_AVAILABLE
    FORGE_MDSPAN_PADDED_LAYOUTS_FULL
    FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_LEFT
    FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_RIGHT
    FORGE_SUBMDSPAN_FULL
    FORGE_SUBMDSPAN_PARTIAL_MACRO
    FORGE_SUBMDSPAN_PARTIAL_CURRENT
    FORGE_SUBMDSPAN_PARTIAL_LEGACY
    FORGE_SUBMDSPAN_PARTIAL_MAPPING_RESULT
    FORGE_SUBMDSPAN_PARTIAL_FUNCTION
    FORGE_LINALG_FULL
    FORGE_LINALG_PARTIAL_MACRO
    FORGE_LINALG_PARTIAL_SETUP
    FORGE_LINALG_PARTIAL_TAG
    FORGE_LINALG_PARTIAL_LAYOUT
    FORGE_LINALG_PARTIAL_MATRIX_PRODUCT)

set(_forge_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
_forge_select_cxx_standard_compile_option(
    _forge_standard_option "${_forge_std}")
if("${_forge_standard_option}" STREQUAL "" AND MSVC)
    if(_forge_std GREATER_EQUAL 23)
        # MSVC versions that provide C++23 through CMake may expose it only
        # through /std:c++latest; /std:c++23 is not a portable MSVC option.
        set(_forge_standard_option "/std:c++latest")
    else()
        set(_forge_standard_option "/std:c++${_forge_std}")
    endif()
endif()
if(NOT "${_forge_standard_option}" STREQUAL "")
    set(CMAKE_REQUIRED_FLAGS
        "${CMAKE_REQUIRED_FLAGS} ${_forge_standard_option}")
endif()
if(MSVC)
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /Zc:__cplusplus")
endif()
unset(_forge_standard_option)

# Three-state decision helper for each backportable feature.
#
#   full    = the COMPLETE required API surface compiles natively
#   partial = ANY trace of a native implementation is present
#
#   full             -> native is complete: stand aside, signal the wrapper.
#   !full && partial -> native is incomplete: stand aside anyway to avoid
#                       injecting the backport on top of partial native
#                       declarations in namespace std (ODR violation), and warn.
#   !full && !partial -> no native at all: inject the backport.
#
# FORGE_FORCE_<F>_BACKPORT overrides the decision and forces injection
# (UB-prone on a partial-native toolchain; used only for testing/diagnosis).
macro(_forge_decide _disp _suffix _full _partial)
    if(FORGE_FORCE_${_suffix}_BACKPORT)
        set(FORGE_NEEDS_BACKPORT TRUE)
        target_compile_definitions(${FORGE_BACKPORT_TARGET} INTERFACE FORGE_FORCE_${_suffix}_BACKPORT=1)
        message(STATUS "CC Forge probe: ${_suffix}=FORCED")
        message(WARNING "CC Forge: ${_disp} backport FORCED on (FORGE_FORCE_${_suffix}_BACKPORT=ON); injecting on top of any native implementation risks ODR violations")
    elseif(${_full})
        target_compile_definitions(${FORGE_BACKPORT_TARGET} INTERFACE FORGE_HAS_NATIVE_${_suffix}=1)
        message(STATUS "CC Forge probe: ${_suffix}=COMPLETE")
        message(STATUS "CC Forge: ${_disp} native support detected - backport disabled")
    elseif(${_partial})
        target_compile_definitions(${FORGE_BACKPORT_TARGET} INTERFACE FORGE_HAS_NATIVE_${_suffix}=1)
        message(STATUS "CC Forge probe: ${_suffix}=PARTIAL")
        message(WARNING "CC Forge: ${_disp} native support is present but INCOMPLETE in C++${_forge_std} mode; Forge stands aside to avoid ODR conflicts. Wait for the toolchain to finish it or select a language mode where these declarations are absent. FORGE_FORCE_${_suffix}_BACKPORT remains a diagnostic-only switch and may fail to compile or violate the ODR on this toolchain.")
    else()
        set(FORGE_NEEDS_BACKPORT TRUE)
        message(STATUS "CC Forge probe: ${_suffix}=BACKPORT")
        message(STATUS "CC Forge: ${_disp} backport enabled")
    endif()
endmacro()

option(FORGE_FORCE_SIMD_BACKPORT "Force the std::simd backport even if (partial) native support exists" OFF)
option(FORGE_FORCE_SENDERS_BACKPORT "Force the std::execution backport even if (partial) native support exists" OFF)
option(FORGE_FORCE_CONSTANT_WRAPPER_BACKPORT "Force the std::constant_wrapper backport even if (partial) native support exists" OFF)
option(FORGE_FORCE_MDSPAN_PADDED_LAYOUTS_BACKPORT "Force the std::mdspan padded-layout backport even if (partial) native support exists" OFF)
option(FORGE_FORCE_SUBMDSPAN_BACKPORT "Force the std::submdspan backport even if (partial) native support exists" OFF)
option(FORGE_FORCE_LINALG_BACKPORT "Force the std::linalg backport even if (partial) native support exists" OFF)

# std::unique_resource is TS v3, not C++26.
check_cxx_source_compiles("
    #include <version>
    #if !defined(__cpp_lib_unique_resource) || __cpp_lib_unique_resource < 202311L
    #error unique_resource not available
    #endif
    int main() { return 0; }
" HAS_STD_UNIQUE_RESOURCE)

set(FORGE_NEEDS_EXPERIMENTAL FALSE)
if(NOT HAS_STD_UNIQUE_RESOURCE)
    set(FORGE_NEEDS_EXPERIMENTAL TRUE)
    message(STATUS "CC Forge: std::unique_resource backport enabled (TS v3)")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeSimd.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeExecution.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeConstantWrapper.cmake")

# The mdspan-dependent backports extend a toolchain-provided base <mdspan>;
# CC Forge does not ship the C++23 mdspan foundation itself.
check_cxx_source_compiles("
    #include <mdspan>
    int main() {
        int data[1]{};
        std::mdspan<int, std::extents<int, 1>> view(data);
        return view[0];
    }
" FORGE_BASE_MDSPAN_AVAILABLE)

if(FORGE_BASE_MDSPAN_AVAILABLE)
    include("${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeMdspanPadded.cmake")

    include("${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeSubmdspan.cmake")

    include("${CMAKE_CURRENT_LIST_DIR}/probes/ForgeProbeLinalg.cmake")
else()
    target_compile_definitions(${FORGE_BACKPORT_TARGET} INTERFACE
        FORGE_NO_BASE_MDSPAN=1)
    message(STATUS "CC Forge probe: MDSPAN_BASE=UNAVAILABLE")
    message(STATUS "CC Forge probe: MDSPAN_PADDED_LAYOUTS=UNAVAILABLE")
    message(STATUS "CC Forge probe: SUBMDSPAN=UNAVAILABLE")
    message(STATUS "CC Forge probe: LINALG=UNAVAILABLE")
    message(WARNING "CC Forge: the configured standard library has no usable base <mdspan>; padded layouts, submdspan, and linalg backports are unavailable")
endif()

set(CMAKE_REQUIRED_FLAGS "${_forge_saved_required_flags}")
