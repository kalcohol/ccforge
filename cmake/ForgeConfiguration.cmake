include("${CMAKE_CURRENT_LIST_DIR}/ForgeProbeFingerprint.cmake")

# One decision set per configure, independent of directory/function scope.
function(_forge_check_configuration out_var)
    _forge_collect_probe_inputs(_inputs _environment "${CMAKE_CXX_STANDARD}")
    list(APPEND _inputs CMAKE_CXX_STANDARD
        FORGE_ROOT_DIR FORGE_INCLUDE_DIR FORGE_BACKPORT_DIR FORGE_CMAKE_DIR
        FORGE_ENABLE_FORGE_RUNTIME FORGE_ENABLE_FORGE_RESOURCE_POLICY
        FORGE_ENABLE_FORGE_IO FORGE_ENABLE_FORGE_IO_URING)
    foreach(_feature SIMD SENDERS CONSTANT_WRAPPER MDSPAN_PADDED_LAYOUTS SUBMDSPAN LINALG HIVE)
        list(APPEND _inputs FORGE_FORCE_${_feature}_BACKPORT)
    endforeach()
    foreach(_name IN LISTS _environment)
        list(APPEND _inputs "environment:${_name}")
    endforeach()
    _forge_collect_backport_probe_sources(_probe_sources)
    _forge_collect_probe_content(_runtime_content "${_FORGE_CURRENT_LIST_DIR}/forge.cmake")
    _forge_collect_probe_content(_backport_content
        "${FORGE_CMAKE_DIR}/ForgeBackportProbes.cmake" ${_probe_sources})
    # Keep diagnostics compact while retaining all resolved paths and digests.
    string(SHA256 _content_runtime "${_runtime_content}")
    string(SHA256 _content_backport "${_backport_content}")
    list(APPEND _inputs content:runtime content:backport)
    get_property(_previous_inputs GLOBAL PROPERTY _FORGE_CONFIGURATION_INPUTS)
    list(APPEND _inputs ${_previous_inputs})
    list(REMOVE_DUPLICATES _inputs)
    list(SORT _inputs)
    get_property(_previous GLOBAL PROPERTY _FORGE_CONFIGURATION_FINGERPRINT)
    set(_payload "")
    set(_differences "")
    foreach(_input IN LISTS _inputs)
        if(_input MATCHES "^content:(.+)$")
            set(_value "${_content_${CMAKE_MATCH_1}}")
        elseif(_input MATCHES "^environment:(.+)$")
            set(_value "$ENV{${CMAKE_MATCH_1}}")
        else()
            _forge_normalize_probe_input(_value "${_input}")
        endif()
        string(LENGTH "${_value}" _length)
        string(APPEND _payload "${_input}:${_length}:${_value}")
        if(_previous)
            get_property(_old GLOBAL PROPERTY _FORGE_CONFIGURATION_${_input})
            if(NOT "${_old}" STREQUAL "${_value}")
                string(APPEND _differences "\n  ${_input}: '${_old}' -> '${_value}'")
            endif()
        else()
            set_property(GLOBAL PROPERTY _FORGE_CONFIGURATION_${_input} "${_value}")
        endif()
    endforeach()
    string(SHA256 _fingerprint "${_payload}")
    if(_previous AND NOT _previous STREQUAL _fingerprint)
        message(FATAL_ERROR
            "CC Forge configuration changed after first inclusion:${_differences}\nUse identical settings for all inclusions, or separate build trees.")
    endif()
    set_property(GLOBAL PROPERTY _FORGE_CONFIGURATION_INPUTS "${_inputs}")
    set_property(GLOBAL PROPERTY _FORGE_CONFIGURATION_FINGERPRINT "${_fingerprint}")
    set(${out_var} "${_previous}" PARENT_SCOPE)
endfunction()

# Only CMake's compile-usage propagation determines which TUs see this guard.
# In particular, a static library's PRIVATE dependencies are link-only for its
# consumers and must not impose a language check on those consumers.
function(_forge_add_language_guard)
    set(FORGE_PROBE_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
    set(FORGE_PROBE_CXX_STANDARD "${CMAKE_CXX_STANDARD}" PARENT_SCOPE)
    set_property(GLOBAL PROPERTY _FORGE_PROBE_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
    # Compare the actual language era, including draft __cplusplus values and
    # MSVC /std:c++latest, rather than assuming final ISO macro values.
    _forge_select_cxx_standard_compile_option(_option "${CMAKE_CXX_STANDARD}")
    if(NOT _option AND MSVC)
        set(_option "/std:c++latest")
    endif()
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${_option}")
    unset(_FORGE_PROBE_LANGUAGE_AFTER_23 CACHE)
    check_cxx_source_compiles("
        #if defined(_MSVC_LANG)
        #define FORGE_LANGUAGE _MSVC_LANG
        #else
        #define FORGE_LANGUAGE __cplusplus
        #endif
        static_assert(FORGE_LANGUAGE > 202302L);
        int main() { return 0; }
    " _FORGE_PROBE_LANGUAGE_AFTER_23)
    if(_FORGE_PROBE_LANGUAGE_AFTER_23)
        set(FORGE_PROBE_LANGUAGE_MODE 26)
    else()
        set(FORGE_PROBE_LANGUAGE_MODE 23)
    endif()
    set(_header "${CMAKE_BINARY_DIR}/CMakeFiles/CCForgeLanguageGuard.h")
    configure_file("${FORGE_CMAKE_DIR}/ForgeLanguageGuard.h.in" "${_header}" @ONLY)
    # A failed compiler invocation must not masquerade as a C++23 result.
    unset(_FORGE_PROBE_LANGUAGE_VALID CACHE)
    check_cxx_source_compiles("
        #include \"${_header}\"
        int main() { return 0; }
    " _FORGE_PROBE_LANGUAGE_VALID)
    if(NOT _FORGE_PROBE_LANGUAGE_VALID)
        message(FATAL_ERROR
            "CC Forge could not validate the probe language mode; check the C++ compiler, CMAKE_CXX_STANDARD and probe flags.")
    endif()
    foreach(_target IN LISTS ARGN)
        set_property(TARGET ${_target} PROPERTY
            FORGE_PROBE_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
        # SHELL keeps -include and its quoted path together during option
        # de-duplication. /FI takes one argument, also for clang-cl frontends.
        # VS may apply CXX options to C in mixed targets; the header is inert in C.
        if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            target_compile_options(${_target} INTERFACE
                "$<$<COMPILE_LANGUAGE:CXX>:/FI${_header}>")
        else()
            target_compile_options(${_target} INTERFACE
                "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include \"${_header}\">")
        endif()
    endforeach()
endfunction()
