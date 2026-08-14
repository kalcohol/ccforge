# Inputs that can change CMake try_compile results must participate in Forge's
# probe cache key. Keep runtime and standard-backport probes on the same key
# shape so reconfiguration cannot leave one family stale.

function(_forge_append_probe_fingerprint_field out_var payload key value)
    string(LENGTH "${key}" _forge_key_length)
    string(LENGTH "${value}" _forge_value_length)
    set(${out_var}
        "${payload}${_forge_key_length}:${key}${_forge_value_length}:${value}"
        PARENT_SCOPE)
endfunction()

function(_forge_append_probe_fingerprint_variable out_var payload variable_name)
    if(DEFINED ${variable_name})
        set(_forge_value "defined:${${variable_name}}")
    else()
        set(_forge_value "undefined:")
    endif()
    _forge_append_probe_fingerprint_field(
        _forge_payload "${payload}" "${variable_name}" "${_forge_value}")
    set(${out_var} "${_forge_payload}" PARENT_SCOPE)
endfunction()

function(_forge_append_probe_file_digest out_var payload key path)
    if(EXISTS "${path}" AND NOT IS_DIRECTORY "${path}")
        file(SHA256 "${path}" _forge_digest)
        set(_forge_value "${path}:${_forge_digest}")
    else()
        set(_forge_value "${path}:missing")
    endif()
    _forge_append_probe_fingerprint_field(
        _forge_payload "${payload}" "${key}" "${_forge_value}")
    set(${out_var} "${_forge_payload}" PARENT_SCOPE)
endfunction()

function(_forge_collect_probe_flag_include_roots out_var)
    set(_forge_include_roots)
    set(_forge_flag_variables CMAKE_REQUIRED_FLAGS)
    get_cmake_property(_forge_variables VARIABLES)
    foreach(_forge_variable IN LISTS _forge_variables)
        if(_forge_variable MATCHES "^CMAKE_CXX_FLAGS(_[A-Za-z0-9_]+)?$")
            list(APPEND _forge_flag_variables "${_forge_variable}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _forge_flag_variables)

    foreach(_forge_variable IN LISTS _forge_flag_variables)
        if(NOT DEFINED ${_forge_variable} OR "${${_forge_variable}}" STREQUAL "")
            continue()
        endif()
        separate_arguments(
            _forge_flag_tokens NATIVE_COMMAND "${${_forge_variable}}")
        set(_forge_expect_include_root OFF)
        foreach(_forge_token IN LISTS _forge_flag_tokens)
            if(_forge_expect_include_root)
                list(APPEND _forge_include_roots "${_forge_token}")
                set(_forge_expect_include_root OFF)
            elseif(_forge_token STREQUAL "-I"
                    OR _forge_token STREQUAL "-isystem"
                    OR _forge_token STREQUAL "-iquote"
                    OR _forge_token STREQUAL "-idirafter"
                    OR _forge_token STREQUAL "-imsvc"
                    OR _forge_token MATCHES "^/[Ii]$"
                    OR _forge_token MATCHES "^/external:[Ii]$")
                set(_forge_expect_include_root ON)
            elseif(_forge_token MATCHES "^-I(.+)$")
                list(APPEND _forge_include_roots "${CMAKE_MATCH_1}")
            elseif(_forge_token MATCHES "^-isystem=?(.+)$")
                set(_forge_include_root "${CMAKE_MATCH_1}")
                string(REGEX REPLACE "^=" "" _forge_include_root
                    "${_forge_include_root}")
                list(APPEND _forge_include_roots "${_forge_include_root}")
            elseif(_forge_token MATCHES "^-iquote=?(.+)$")
                set(_forge_include_root "${CMAKE_MATCH_1}")
                string(REGEX REPLACE "^=" "" _forge_include_root
                    "${_forge_include_root}")
                list(APPEND _forge_include_roots "${_forge_include_root}")
            elseif(_forge_token MATCHES "^-idirafter=?(.+)$")
                set(_forge_include_root "${CMAKE_MATCH_1}")
                string(REGEX REPLACE "^=" "" _forge_include_root
                    "${_forge_include_root}")
                list(APPEND _forge_include_roots "${_forge_include_root}")
            elseif(_forge_token MATCHES "^-imsvc=?(.+)$")
                set(_forge_include_root "${CMAKE_MATCH_1}")
                string(REGEX REPLACE "^=" "" _forge_include_root
                    "${_forge_include_root}")
                list(APPEND _forge_include_roots "${_forge_include_root}")
            elseif(_forge_token MATCHES "^/[Ii](.+)$")
                list(APPEND _forge_include_roots "${CMAKE_MATCH_1}")
            elseif(_forge_token MATCHES "^/external:[Ii](.+)$")
                list(APPEND _forge_include_roots "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endforeach()

    set(${out_var} "${_forge_include_roots}" PARENT_SCOPE)
endfunction()

function(_forge_append_probe_header_digests out_var payload probe_file)
    set(_forge_payload "${payload}")
    if(NOT EXISTS "${probe_file}" OR IS_DIRECTORY "${probe_file}")
        set(${out_var} "${_forge_payload}" PARENT_SCOPE)
        return()
    endif()

    file(STRINGS "${probe_file}" _forge_include_lines
        REGEX "#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]")
    set(_forge_headers)
    foreach(_forge_line IN LISTS _forge_include_lines)
        string(REGEX REPLACE
            ".*[<\"]([^>\"]+)[>\"].*" "\\1" _forge_header "${_forge_line}")
        list(APPEND _forge_headers "${_forge_header}")
    endforeach()
    list(REMOVE_DUPLICATES _forge_headers)

    _forge_collect_probe_flag_include_roots(_forge_flag_include_roots)
    set(_forge_include_roots
        ${CMAKE_REQUIRED_INCLUDES}
        ${CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES}
        ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}
        ${CMAKE_INCLUDE_PATH}
        ${_forge_flag_include_roots})
    foreach(_forge_environment_path INCLUDE CPATH CPLUS_INCLUDE_PATH CXX_INCLUDE_PATH)
        if(NOT "$ENV{${_forge_environment_path}}" STREQUAL "")
            file(TO_CMAKE_PATH "$ENV{${_forge_environment_path}}" _forge_environment_roots)
            list(APPEND _forge_include_roots ${_forge_environment_roots})
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _forge_include_roots)

    foreach(_forge_header IN LISTS _forge_headers)
        # Hash every candidate: generator-specific flag ordering decides which
        # same-named header wins, and extra invalidation is safer than staleness.
        set(_forge_found_header OFF)
        set(_forge_candidate_index 0)
        foreach(_forge_root IN LISTS _forge_include_roots)
            if(_forge_root MATCHES "^\\$<")
                continue()
            endif()
            if(IS_ABSOLUTE "${_forge_root}")
                set(_forge_candidate "${_forge_root}/${_forge_header}")
            else()
                set(_forge_candidate
                    "${CMAKE_CURRENT_SOURCE_DIR}/${_forge_root}/${_forge_header}")
            endif()
            if(EXISTS "${_forge_candidate}" AND NOT IS_DIRECTORY "${_forge_candidate}")
                _forge_append_probe_file_digest(
                    _forge_payload "${_forge_payload}"
                    "probe-header:${_forge_header}:${_forge_candidate_index}"
                    "${_forge_candidate}")
                set(_forge_found_header ON)
                math(EXPR _forge_candidate_index "${_forge_candidate_index} + 1")
            endif()
        endforeach()

        if(NOT _forge_found_header)
            _forge_append_probe_fingerprint_field(
                _forge_payload "${_forge_payload}"
                "probe-header:${_forge_header}" "unresolved")
        endif()
    endforeach()

    set(${out_var} "${_forge_payload}" PARENT_SCOPE)
endfunction()

function(_forge_compute_probe_fingerprint out_var language_standard)
    set(_forge_probe_callsite "${CMAKE_CURRENT_LIST_FILE}")
    set(_forge_fingerprint_module "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
    set(_forge_payload)

    _forge_append_probe_fingerprint_field(
        _forge_payload "${_forge_payload}" "fingerprint-version" "2")
    _forge_append_probe_fingerprint_field(
        _forge_payload "${_forge_payload}" "language-standard" "${language_standard}")

    set(_forge_scalar_inputs
        CMAKE_CXX_COMPILER
        CMAKE_CXX_COMPILER_ARG1
        CMAKE_CXX_COMPILER_ID
        CMAKE_CXX_COMPILER_VERSION
        CMAKE_CXX_COMPILER_TARGET
        CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN
        CMAKE_CXX_COMPILER_FRONTEND_VARIANT
        CMAKE_CXX_COMPILER_AR
        CMAKE_CXX_COMPILER_RANLIB
        CMAKE_CXX_COMPILER_LAUNCHER
        CMAKE_CXX_LINKER_LAUNCHER
        CMAKE_LINKER
        CMAKE_LINKER_TYPE
        CMAKE_CXX_LINKER_TYPE
        CMAKE_AR
        CMAKE_RANLIB
        CMAKE_CXX_STANDARD_REQUIRED
        CMAKE_CXX_EXTENSIONS
        CMAKE_CXX_SCAN_FOR_MODULES
        CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
        CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES
        CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES
        CMAKE_CXX_IMPLICIT_LINK_LIBRARIES
        CMAKE_CXX_STANDARD_LIBRARIES
        CMAKE_MSVC_RUNTIME_LIBRARY
        CMAKE_POSITION_INDEPENDENT_CODE
        CMAKE_BUILD_TYPE
        CMAKE_CONFIGURATION_TYPES
        CMAKE_TRY_COMPILE_CONFIGURATION
        CMAKE_TRY_COMPILE_NO_PLATFORM_VARIABLES
        CMAKE_TRY_COMPILE_TARGET_TYPE
        CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
        CMAKE_GENERATOR
        CMAKE_GENERATOR_PLATFORM
        CMAKE_GENERATOR_TOOLSET
        CMAKE_MAKE_PROGRAM
        CMAKE_SYSROOT
        CMAKE_SYSROOT_COMPILE
        CMAKE_SYSROOT_LINK
        CMAKE_OSX_ARCHITECTURES
        CMAKE_SYSTEM_NAME
        CMAKE_SYSTEM_PROCESSOR
        CMAKE_TOOLCHAIN_FILE
        CMAKE_REQUIRED_FLAGS
        CMAKE_REQUIRED_DEFINITIONS
        CMAKE_REQUIRED_INCLUDES
        CMAKE_REQUIRED_LINK_DIRECTORIES
        CMAKE_REQUIRED_LINK_OPTIONS
        CMAKE_REQUIRED_LIBRARIES)
    foreach(_forge_variable IN LISTS _forge_scalar_inputs)
        _forge_append_probe_fingerprint_variable(
            _forge_payload "${_forge_payload}" "${_forge_variable}")
    endforeach()

    foreach(_forge_variable
            CMAKE_CXX${language_standard}_STANDARD_COMPILE_OPTION
            CMAKE_CXX${language_standard}_EXTENSION_COMPILE_OPTION)
        _forge_append_probe_fingerprint_variable(
            _forge_payload "${_forge_payload}" "${_forge_variable}")
    endforeach()

    foreach(_forge_variable IN LISTS CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)
        if(DEFINED ${_forge_variable})
            set(_forge_value "defined:${${_forge_variable}}")
        else()
            set(_forge_value "undefined:")
        endif()
        _forge_append_probe_fingerprint_field(
            _forge_payload "${_forge_payload}"
            "try-compile-platform:${_forge_variable}" "${_forge_value}")
    endforeach()

    get_cmake_property(_forge_variables VARIABLES)
    list(SORT _forge_variables)
    foreach(_forge_variable IN LISTS _forge_variables)
        if(_forge_variable MATCHES
                "^CMAKE_(CXX|EXE_LINKER|STATIC_LINKER)_FLAGS(_[A-Za-z0-9_]+)?$")
            _forge_append_probe_fingerprint_variable(
                _forge_payload "${_forge_payload}" "${_forge_variable}")
        endif()
    endforeach()

    foreach(_forge_environment_variable
            INCLUDE CPATH CPLUS_INCLUDE_PATH CXX_INCLUDE_PATH
            LIB LIBPATH LIBRARY_PATH)
        _forge_append_probe_fingerprint_field(
            _forge_payload "${_forge_payload}"
            "environment:${_forge_environment_variable}"
            "$ENV{${_forge_environment_variable}}")
    endforeach()

    if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_TOOLCHAIN_FILE STREQUAL "")
        if(IS_ABSOLUTE "${CMAKE_TOOLCHAIN_FILE}")
            set(_forge_toolchain_file "${CMAKE_TOOLCHAIN_FILE}")
        elseif(EXISTS "${CMAKE_BINARY_DIR}/${CMAKE_TOOLCHAIN_FILE}")
            set(_forge_toolchain_file
                "${CMAKE_BINARY_DIR}/${CMAKE_TOOLCHAIN_FILE}")
        else()
            set(_forge_toolchain_file
                "${CMAKE_SOURCE_DIR}/${CMAKE_TOOLCHAIN_FILE}")
        endif()
        _forge_append_probe_file_digest(
            _forge_payload "${_forge_payload}" "toolchain-file"
            "${_forge_toolchain_file}")
    endif()

    _forge_append_probe_file_digest(
        _forge_payload "${_forge_payload}" "probe-callsite" "${_forge_probe_callsite}")
    _forge_append_probe_file_digest(
        _forge_payload "${_forge_payload}" "fingerprint-module"
        "${_forge_fingerprint_module}")
    _forge_append_probe_header_digests(
        _forge_payload "${_forge_payload}" "${_forge_probe_callsite}")

    string(SHA256 _forge_fingerprint "${_forge_payload}")
    set(${out_var} "v2:${_forge_fingerprint}" PARENT_SCOPE)
endfunction()

# Positive checks remain cached while their full fingerprint is stable. A
# negative check is retried on the next configure so transient compiler,
# launcher, filesystem, or resource failures cannot become permanent facts.
function(_forge_refresh_probe_cache fingerprint_var current_fingerprint)
    set(_forge_probe_vars ${ARGN})
    set(_forge_invalidate_all OFF)
    if(NOT DEFINED ${fingerprint_var}
            OR NOT "${${fingerprint_var}}" STREQUAL "${current_fingerprint}")
        set(_forge_invalidate_all ON)
    endif()

    foreach(_forge_probe_var IN LISTS _forge_probe_vars)
        set(_forge_invalidate_probe ${_forge_invalidate_all})
        if(NOT _forge_invalidate_probe
                AND DEFINED ${_forge_probe_var}
                AND NOT "${${_forge_probe_var}}")
            set(_forge_invalidate_probe ON)
        endif()
        if(_forge_invalidate_probe)
            unset(${_forge_probe_var} CACHE)
            unset(${_forge_probe_var} PARENT_SCOPE)
        endif()
    endforeach()

    set(${fingerprint_var} "${current_fingerprint}"
        CACHE INTERNAL "CC Forge probe fingerprint" FORCE)
endfunction()
