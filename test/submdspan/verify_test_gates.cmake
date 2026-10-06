foreach(required_var IN ITEMS FORGE_ROOT_DIR BINARY_ROOT GENERATOR)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()

string(SHA256 log_id "${BINARY_ROOT}")
string(SUBSTRING "${log_id}" 0 12 log_id)
set(log_dir "${FORGE_ROOT_DIR}/build/review/submdspan-gates/${log_id}")
file(MAKE_DIRECTORY "${log_dir}")

foreach(gate_case IN ITEMS
        backport complete_partial_dependencies complete_all mixed_native_padded
        partial_available partial_unavailable
        positive_control_failure negative_control_success)
    set(args
        -S "${FORGE_ROOT_DIR}/test/submdspan/gate_fixture"
        -B "${BINARY_ROOT}/${gate_case}"
        -G "${GENERATOR}"
        -DFORGE_ROOT_DIR=${FORGE_ROOT_DIR}
        -DGATE_CASE=${gate_case})
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
    if(gate_case STREQUAL "positive_control_failure")
        if(result EQUAL 0 OR NOT error MATCHES "padded conversion probes require compatible conversions to compile")
            message(FATAL_ERROR "Positive-control failure was not enforced; see ${log_file}")
        endif()
    elseif(gate_case STREQUAL "negative_control_success")
        if(result EQUAL 0 OR NOT error MATCHES "padded conversion must reject left_unpadded_to_padded")
            message(FATAL_ERROR "Negative-control rejection was not enforced; see ${log_file}")
        endif()
    elseif(NOT result EQUAL 0)
        message(FATAL_ERROR "Submdspan gate ${gate_case} failed; see ${log_file}")
    endif()
endforeach()
