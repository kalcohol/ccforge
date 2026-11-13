foreach(required_var IN ITEMS FORGE_ROOT_DIR BINARY_ROOT GENERATOR)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()

string(SHA256 log_id "${BINARY_ROOT}")
string(SUBSTRING "${log_id}" 0 12 log_id)
set(log_dir "${FORGE_ROOT_DIR}/build/review/hive-gates/${log_id}")
file(MAKE_DIRECTORY "${log_dir}")

foreach(gate_case IN ITEMS backport native_complete native_partial forced_complete forced_partial missing_portable)
    set(fixture_case "${gate_case}")
    set(drop_portable OFF)
    if(gate_case STREQUAL "missing_portable")
        set(fixture_case native_complete)
        set(drop_portable ON)
    endif()
    set(args
        -S "${FORGE_ROOT_DIR}/test/hive/gate_fixture"
        -B "${BINARY_ROOT}/${gate_case}"
        -G "${GENERATOR}"
        -DFORGE_ROOT_DIR=${FORGE_ROOT_DIR}
        -DGATE_CASE=${fixture_case}
        -DFIXTURE_DROP_PORTABLE=${drop_portable})
    if(DEFINED GENERATOR_PLATFORM AND NOT GENERATOR_PLATFORM STREQUAL "")
        list(APPEND args -A "${GENERATOR_PLATFORM}")
    endif()
    if(DEFINED GENERATOR_TOOLSET AND NOT GENERATOR_TOOLSET STREQUAL "")
        list(APPEND args -T "${GENERATOR_TOOLSET}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${args}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    set(log_file "${log_dir}/${gate_case}.log")
    file(WRITE "${log_file}" "${output}\n${error}")
    if(drop_portable)
        if(result EQUAL 0 OR NOT error MATCHES "Gate native_complete: expected")
            message(FATAL_ERROR "Hive gate negative control did not detect missing portable registration; see ${log_file}")
        endif()
    elseif(NOT result EQUAL 0)
        message(FATAL_ERROR "Hive gate ${gate_case} failed; see ${log_file}")
    endif()
endforeach()
