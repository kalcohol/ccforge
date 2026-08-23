if(NOT DEFINED FORGE_FINGERPRINT_MODULE)
    message(FATAL_ERROR "FORGE_FINGERPRINT_MODULE is required")
endif()
if(NOT DEFINED FORGE_FINGERPRINT_WORK_DIR)
    message(FATAL_ERROR "FORGE_FINGERPRINT_WORK_DIR is required")
endif()
include("${FORGE_FINGERPRINT_MODULE}")

set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std=forge-strict")
set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "-std=forge-extended")
set(CMAKE_CXX_EXTENSIONS OFF)
_forge_select_cxx_standard_compile_option(_forge_strict_option 23)
if(NOT _forge_strict_option STREQUAL "-std=forge-strict")
    message(FATAL_ERROR "Probe did not select CMake's strict standard option")
endif()
set(CMAKE_CXX_EXTENSIONS ON)
_forge_select_cxx_standard_compile_option(_forge_extended_option 23)
if(NOT _forge_extended_option STREQUAL "-std=forge-extended")
    message(FATAL_ERROR "Probe did not select CMake's extension standard option")
endif()

function(_forge_require_fingerprint_change description before after)
    if("${before}" STREQUAL "${after}")
        message(FATAL_ERROR "Probe fingerprint ignored ${description}")
    endif()
endfunction()

set(CMAKE_REQUIRED_DEFINITIONS "ab;c")
_forge_compute_probe_fingerprint(_forge_boundary_a 23)
set(CMAKE_REQUIRED_DEFINITIONS "a;bc")
_forge_compute_probe_fingerprint(_forge_boundary_b 23)
_forge_require_fingerprint_change(
    "list boundaries" "${_forge_boundary_a}" "${_forge_boundary_b}")

set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "/forge/include/one")
_forge_compute_probe_fingerprint(_forge_include_a 23)
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "/forge/include/two")
_forge_compute_probe_fingerprint(_forge_include_b 23)
_forge_require_fingerprint_change(
    "standard include directories" "${_forge_include_a}" "${_forge_include_b}")

set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")
_forge_compute_probe_fingerprint(_forge_osx_target_a 23)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
_forge_compute_probe_fingerprint(_forge_osx_target_b 23)
_forge_require_fingerprint_change(
    "macOS deployment target"
    "${_forge_osx_target_a}" "${_forge_osx_target_b}")

set(CMAKE_OSX_SYSROOT "/forge/macos-sdk/one")
_forge_compute_probe_fingerprint(_forge_osx_sysroot_a 23)
set(CMAKE_OSX_SYSROOT "/forge/macos-sdk/two")
_forge_compute_probe_fingerprint(_forge_osx_sysroot_b 23)
_forge_require_fingerprint_change(
    "macOS SDK root"
    "${_forge_osx_sysroot_a}" "${_forge_osx_sysroot_b}")

set(CMAKE_CXX_COMPILER_LAUNCHER "launcher-a;--flag")
_forge_compute_probe_fingerprint(_forge_launcher_a 23)
set(CMAKE_CXX_COMPILER_LAUNCHER "launcher-b;--flag")
_forge_compute_probe_fingerprint(_forge_launcher_b 23)
_forge_require_fingerprint_change(
    "compiler launcher" "${_forge_launcher_a}" "${_forge_launcher_b}")

set(CMAKE_CONFIGURATION_TYPES "Profile")
set(CMAKE_CXX_FLAGS_PROFILE "-DFORGE_PROFILE=1")
_forge_compute_probe_fingerprint(_forge_config_a 23)
set(CMAKE_CXX_FLAGS_PROFILE "-DFORGE_PROFILE=2")
_forge_compute_probe_fingerprint(_forge_config_b 23)
_forge_require_fingerprint_change(
    "custom configuration flags" "${_forge_config_a}" "${_forge_config_b}")

set(CMAKE_REQUIRED_LINK_DIRECTORIES "/forge/link/one;/forge/link/common")
_forge_compute_probe_fingerprint(_forge_link_a 23)
set(CMAKE_REQUIRED_LINK_DIRECTORIES "/forge/link/two;/forge/link/common")
_forge_compute_probe_fingerprint(_forge_link_b 23)
_forge_require_fingerprint_change(
    "required link directories" "${_forge_link_a}" "${_forge_link_b}")

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES FORGE_B03_PLATFORM_INPUT)
set(FORGE_B03_PLATFORM_INPUT platform-a)
_forge_compute_probe_fingerprint(_forge_platform_a 23)
set(FORGE_B03_PLATFORM_INPUT platform-b)
_forge_compute_probe_fingerprint(_forge_platform_b 23)
_forge_require_fingerprint_change(
    "try-compile platform variable values"
    "${_forge_platform_a}" "${_forge_platform_b}")

set(CMAKE_CXX_STANDARD_REQUIRED OFF)
_forge_compute_probe_fingerprint(_forge_standard_required_a 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
_forge_compute_probe_fingerprint(_forge_standard_required_b 23)
_forge_require_fingerprint_change(
    "required language standard"
    "${_forge_standard_required_a}" "${_forge_standard_required_b}")

set(CMAKE_TRY_COMPILE_NO_PLATFORM_VARIABLES OFF)
_forge_compute_probe_fingerprint(_forge_platform_forwarding_a 23)
set(CMAKE_TRY_COMPILE_NO_PLATFORM_VARIABLES ON)
_forge_compute_probe_fingerprint(_forge_platform_forwarding_b 23)
_forge_require_fingerprint_change(
    "try-compile platform forwarding"
    "${_forge_platform_forwarding_a}" "${_forge_platform_forwarding_b}")

set(CMAKE_LINKER_TYPE DEFAULT)
_forge_compute_probe_fingerprint(_forge_linker_type_a 23)
set(CMAKE_LINKER_TYPE SYSTEM)
_forge_compute_probe_fingerprint(_forge_linker_type_b 23)
_forge_require_fingerprint_change(
    "linker type" "${_forge_linker_type_a}" "${_forge_linker_type_b}")

set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std=forge-a")
_forge_compute_probe_fingerprint(_forge_standard_option_a 23)
set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std=forge-b")
_forge_compute_probe_fingerprint(_forge_standard_option_b 23)
_forge_require_fingerprint_change(
    "language-standard option"
    "${_forge_standard_option_a}" "${_forge_standard_option_b}")

file(REMOVE_RECURSE "${FORGE_FINGERPRINT_WORK_DIR}")
file(MAKE_DIRECTORY "${FORGE_FINGERPRINT_WORK_DIR}")
set(CMAKE_TOOLCHAIN_FILE
    "${FORGE_FINGERPRINT_WORK_DIR}/forge-b03-toolchain.cmake")
file(WRITE "${CMAKE_TOOLCHAIN_FILE}" "set(FORGE_B03_TOOLCHAIN revision-a)\n")
_forge_compute_probe_fingerprint(_forge_toolchain_a 23)
file(WRITE "${CMAKE_TOOLCHAIN_FILE}" "set(FORGE_B03_TOOLCHAIN revision-b)\n")
_forge_compute_probe_fingerprint(_forge_toolchain_b 23)
_forge_require_fingerprint_change(
    "same-path toolchain content" "${_forge_toolchain_a}" "${_forge_toolchain_b}")

set(FORGE_B03_FAILED "" CACHE INTERNAL "failed fixture" FORCE)
set(FORGE_B03_PASSED 1 CACHE INTERNAL "passed fixture" FORCE)
set(FORGE_B03_FINGERPRINT stable CACHE INTERNAL "fixture fingerprint" FORCE)
_forge_refresh_probe_cache(
    FORGE_B03_FINGERPRINT stable FORGE_B03_FAILED FORGE_B03_PASSED)
get_property(_forge_failed_cached CACHE FORGE_B03_FAILED PROPERTY TYPE SET)
get_property(_forge_passed_cached CACHE FORGE_B03_PASSED PROPERTY TYPE SET)
if(_forge_failed_cached)
    message(FATAL_ERROR "A failed probe remained cached")
endif()
if(NOT _forge_passed_cached)
    message(FATAL_ERROR "A stable successful probe was unnecessarily invalidated")
endif()

_forge_refresh_probe_cache(
    FORGE_B03_FINGERPRINT changed FORGE_B03_PASSED)
get_property(_forge_passed_cached CACHE FORGE_B03_PASSED PROPERTY TYPE SET)
if(_forge_passed_cached)
    message(FATAL_ERROR "A fingerprint change did not invalidate a successful probe")
endif()
