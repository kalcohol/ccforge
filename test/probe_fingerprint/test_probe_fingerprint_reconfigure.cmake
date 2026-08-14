foreach(_forge_required
        FORGE_FINGERPRINT_MODULE
        FORGE_FINGERPRINT_FIXTURE
        FORGE_FINGERPRINT_WORK_DIR
        FORGE_TEST_CXX_COMPILER)
    if(NOT DEFINED ${_forge_required})
        message(FATAL_ERROR "${_forge_required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${FORGE_FINGERPRINT_WORK_DIR}")
set(_forge_source_dir "${FORGE_FINGERPRINT_WORK_DIR}/source")
set(_forge_build_dir "${FORGE_FINGERPRINT_WORK_DIR}/build")
file(MAKE_DIRECTORY "${_forge_source_dir}")
file(COPY "${FORGE_FINGERPRINT_FIXTURE}/" DESTINATION "${_forge_source_dir}")
file(COPY "${FORGE_FINGERPRINT_MODULE}" DESTINATION "${_forge_source_dir}")

set(_forge_configure_command
    "${CMAKE_COMMAND}"
    -S "${_forge_source_dir}"
    -B "${_forge_build_dir}"
    "-DCMAKE_CXX_COMPILER=${FORGE_TEST_CXX_COMPILER}")
if(DEFINED FORGE_TEST_GENERATOR AND NOT FORGE_TEST_GENERATOR STREQUAL "")
    list(APPEND _forge_configure_command -G "${FORGE_TEST_GENERATOR}")
endif()
if(DEFINED FORGE_TEST_GENERATOR_PLATFORM
        AND NOT FORGE_TEST_GENERATOR_PLATFORM STREQUAL "")
    list(APPEND _forge_configure_command -A "${FORGE_TEST_GENERATOR_PLATFORM}")
endif()
if(DEFINED FORGE_TEST_GENERATOR_TOOLSET
        AND NOT FORGE_TEST_GENERATOR_TOOLSET STREQUAL "")
    list(APPEND _forge_configure_command -T "${FORGE_TEST_GENERATOR_TOOLSET}")
endif()

function(_forge_configure_fixture out_log)
    execute_process(
        COMMAND ${_forge_configure_command}
        RESULT_VARIABLE _forge_result
        OUTPUT_VARIABLE _forge_stdout
        ERROR_VARIABLE _forge_stderr)
    set(_forge_log "${_forge_stdout}\n${_forge_stderr}")
    if(NOT _forge_result EQUAL 0)
        message(FATAL_ERROR "Fingerprint fixture configure failed:\n${_forge_log}")
    endif()
    set(${out_log} "${_forge_log}" PARENT_SCOPE)
endfunction()

function(_forge_read_fixture_result out_result out_fingerprint)
    file(STRINGS "${_forge_build_dir}/probe-result.txt" _forge_lines)
    foreach(_forge_line IN LISTS _forge_lines)
        if(_forge_line MATCHES "^result=(.*)$")
            set(_forge_result "${CMAKE_MATCH_1}")
        elseif(_forge_line MATCHES "^fingerprint=(.*)$")
            set(_forge_fingerprint "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(NOT DEFINED _forge_result OR NOT DEFINED _forge_fingerprint)
        message(FATAL_ERROR "Fingerprint fixture did not write a complete result")
    endif()
    set(${out_result} "${_forge_result}" PARENT_SCOPE)
    set(${out_fingerprint} "${_forge_fingerprint}" PARENT_SCOPE)
endfunction()

function(_forge_require_probe_ran description log)
    if(NOT "${log}" MATCHES "Performing Test FORGE_B03_PROBE")
        message(FATAL_ERROR "Probe did not run after ${description}")
    endif()
endfunction()

_forge_configure_fixture(_forge_log)
_forge_read_fixture_result(_forge_result _forge_fingerprint_a)
if(NOT _forge_result STREQUAL "FALSE")
    message(FATAL_ERROR "Initial fixture probe unexpectedly passed")
endif()

# No input changes: a failed check must still be retried rather than becoming
# an indefinite cache entry.
_forge_configure_fixture(_forge_log)
_forge_require_probe_ran("an unchanged failure" "${_forge_log}")

# Change a directly included header at the same path. The probe must rerun and
# observe the new declaration.
file(WRITE "${_forge_source_dir}/include/forge_b03_header.hpp"
    "#define FORGE_B03_HEADER_VALUE 1\n")
_forge_configure_fixture(_forge_log)
_forge_require_probe_ran("same-path header mutation" "${_forge_log}")
_forge_read_fixture_result(_forge_result _forge_fingerprint_b)
if(NOT _forge_result STREQUAL "TRUE")
    message(FATAL_ERROR "Same-path header mutation left the failed result cached")
endif()
if(_forge_fingerprint_a STREQUAL _forge_fingerprint_b)
    message(FATAL_ERROR "Same-path header content did not change the fingerprint")
endif()

# A successful check remains cached while its fingerprint is stable.
_forge_configure_fixture(_forge_log)
if("${_forge_log}" MATCHES "Performing Test FORGE_B03_PROBE")
    message(FATAL_ERROR "Stable successful probe was rerun")
endif()

# Direct headers reached only through a stable compiler -I or /I flag still
# participate by content, not just by the unchanged flag spelling.
file(APPEND
    "${_forge_source_dir}/flag-include/version"
    "\n// same-path flag include revision\n")
_forge_configure_fixture(_forge_log)
_forge_require_probe_ran("same-path flag include mutation" "${_forge_log}")
_forge_read_fixture_result(_forge_result _forge_fingerprint_c)
if(NOT _forge_result STREQUAL "TRUE")
    message(FATAL_ERROR "Flag include mutation changed a valid probe result")
endif()
if(_forge_fingerprint_b STREQUAL _forge_fingerprint_c)
    message(FATAL_ERROR "Flag include content did not change the fingerprint")
endif()

# Removing and restoring a dependency at the same path must not leave either
# a successful or a failed answer pinned in the cache.
file(REMOVE "${_forge_source_dir}/flag-include/version")
_forge_configure_fixture(_forge_log)
_forge_require_probe_ran("direct header removal" "${_forge_log}")
_forge_read_fixture_result(_forge_result _forge_fingerprint_d)
if(NOT _forge_result STREQUAL "FALSE")
    message(FATAL_ERROR "Removed direct header left a successful probe cached")
endif()
if(_forge_fingerprint_c STREQUAL _forge_fingerprint_d)
    message(FATAL_ERROR "Direct header removal did not change the fingerprint")
endif()

file(WRITE "${_forge_source_dir}/flag-include/version"
    "#define FORGE_B03_SHADOWED_VERSION 1\n")
_forge_configure_fixture(_forge_log)
_forge_require_probe_ran("direct header restoration" "${_forge_log}")
_forge_read_fixture_result(_forge_result _forge_fingerprint_e)
if(NOT _forge_result STREQUAL "TRUE")
    message(FATAL_ERROR "Restored direct header left a failed probe cached")
endif()
if(_forge_fingerprint_d STREQUAL _forge_fingerprint_e)
    message(FATAL_ERROR "Direct header restoration did not change the fingerprint")
endif()

# The callsite owns the embedded C++ probe source, so changing that module at
# the same path must invalidate the result.
file(APPEND "${_forge_source_dir}/probe.cmake" "\n# probe source revision\n")
_forge_configure_fixture(_forge_log)
_forge_require_probe_ran("probe module mutation" "${_forge_log}")
_forge_read_fixture_result(_forge_result _forge_fingerprint_f)
if(_forge_fingerprint_e STREQUAL _forge_fingerprint_f)
    message(FATAL_ERROR "Probe module content did not change the fingerprint")
endif()

# Changes to the fingerprint implementation itself also invalidate prior
# results, even when its path is unchanged.
file(APPEND "${_forge_source_dir}/ForgeProbeFingerprint.cmake"
    "\n# fingerprint module revision\n")
_forge_configure_fixture(_forge_log)
_forge_require_probe_ran("fingerprint module mutation" "${_forge_log}")
_forge_read_fixture_result(_forge_result _forge_fingerprint_g)
if(_forge_fingerprint_f STREQUAL _forge_fingerprint_g)
    message(FATAL_ERROR "Fingerprint module content did not change the fingerprint")
endif()
