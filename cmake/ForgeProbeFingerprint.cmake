# Inputs that can change CMake try_compile results must participate in Forge's
# probe cache key. Keep runtime and standard-backport probes on the same key
# shape so reconfiguration cannot leave one family stale.

function(_forge_compute_probe_fingerprint out_var language_standard)
    set(_forge_config_flags)
    foreach(_forge_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        string(APPEND _forge_config_flags
            "|${CMAKE_CXX_FLAGS_${_forge_config}}")
    endforeach()

    set(_forge_fingerprint
        "${language_standard}"
        "|${CMAKE_CXX_COMPILER}"
        "|${CMAKE_CXX_COMPILER_ID}"
        "|${CMAKE_CXX_COMPILER_VERSION}"
        "|${CMAKE_CXX_COMPILER_TARGET}"
        "|${CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN}"
        "|${CMAKE_CXX_EXTENSIONS}"
        "|${CMAKE_CXX_SCAN_FOR_MODULES}"
        "|${CMAKE_CXX_FLAGS}"
        "${_forge_config_flags}"
        "|${CMAKE_BUILD_TYPE}"
        "|${CMAKE_CONFIGURATION_TYPES}"
        "|${CMAKE_TRY_COMPILE_CONFIGURATION}"
        "|${CMAKE_TRY_COMPILE_TARGET_TYPE}"
        "|${CMAKE_GENERATOR}"
        "|${CMAKE_GENERATOR_PLATFORM}"
        "|${CMAKE_GENERATOR_TOOLSET}"
        "|${CMAKE_SYSROOT}"
        "|${CMAKE_OSX_ARCHITECTURES}"
        "|${CMAKE_SYSTEM_NAME}"
        "|${CMAKE_SYSTEM_PROCESSOR}"
        "|${CMAKE_REQUIRED_FLAGS}"
        "|${CMAKE_REQUIRED_DEFINITIONS}"
        "|${CMAKE_REQUIRED_INCLUDES}"
        "|${CMAKE_REQUIRED_LINK_OPTIONS}"
        "|${CMAKE_REQUIRED_LIBRARIES}")
    string(JOIN "" _forge_fingerprint ${_forge_fingerprint})
    set(${out_var} "${_forge_fingerprint}" PARENT_SCOPE)
endfunction()
