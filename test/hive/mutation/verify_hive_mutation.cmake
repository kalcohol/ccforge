# Scratch implementation-mutation proof for the independent hive model.
# No source-tree file is written.  A normal positive control must succeed and
# the same model must reject the copied, size-corrupted implementation.
foreach(required IN ITEMS
        FORGE_ROOT_DIR BINARY_ROOT CXX_COMPILER MODEL_SOURCE RUNTIME_INCLUDE_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "hive mutation: ${required} is required")
    endif()
endforeach()
if(NOT DEFINED CXX_STANDARD OR CXX_STANDARD LESS 23)
    set(CXX_STANDARD 23)
endif()
if(NOT EXISTS "${MODEL_SOURCE}")
    message(FATAL_ERROR "hive mutation: model source is missing: ${MODEL_SOURCE}")
endif()
set(source_header "${FORGE_ROOT_DIR}/backport/cpp26/hive.hpp")
if(NOT EXISTS "${source_header}")
    message(FATAL_ERROR "hive mutation: required implementation header is missing: ${source_header}")
endif()
file(SHA256 "${source_header}" original_hash)

# Deliberately select one explicit implementation statement.  If the
# implementation changes shape, this test fails loudly and the mutation must be
# updated; it must never degrade into a SKIP or a test-only failure.
set(mutation_anchor
    "    control->capacity += block->capacity;\n    ++control->size;")
set(mutation_replacement
    "    control->capacity += block->capacity;\n    control->size += 2;")

set(scratch "${BINARY_ROOT}/scratch")
file(REMOVE_RECURSE "${scratch}")
file(MAKE_DIRECTORY "${scratch}")
set(normal_backport "${scratch}/normal-backport")
set(mutant_backport "${scratch}/mutant-backport")
file(COPY "${FORGE_ROOT_DIR}/backport/" DESTINATION "${normal_backport}")
file(COPY "${FORGE_ROOT_DIR}/backport/" DESTINATION "${mutant_backport}")

set(mutated_header "${mutant_backport}/cpp26/hive.hpp")
file(READ "${mutated_header}" implementation)
string(FIND "${implementation}" "${mutation_anchor}" first_anchor)
if(first_anchor EQUAL -1)
    message(FATAL_ERROR
        "hive mutation: explicit size anchor '${mutation_anchor}' is absent; mutation proof is invalid")
endif()
string(LENGTH "${mutation_anchor}" anchor_length)
math(EXPR remaining_offset "${first_anchor} + ${anchor_length}")
string(SUBSTRING "${implementation}" ${remaining_offset} -1 remaining)
string(FIND "${remaining}" "${mutation_anchor}" second_anchor)
if(NOT second_anchor EQUAL -1)
    message(FATAL_ERROR
        "hive mutation: size anchor '${mutation_anchor}' is not unique; refusing an ambiguous mutation")
endif()
string(REPLACE "${mutation_anchor}" "${mutation_replacement}" implementation
    "${implementation}")
file(WRITE "${mutated_header}" "${implementation}")

if(DEFINED EXTRA_FLAGS AND NOT EXTRA_FLAGS STREQUAL "")
    string(REPLACE "|" ";" extra_flags "${EXTRA_FLAGS}")
else()
    set(extra_flags)
endif()

function(hive_compile_model case_name backport_root executable_out)
    set(executable "${scratch}/${case_name}/hive-model")
    if(WIN32)
        string(APPEND executable ".exe")
    endif()
    file(MAKE_DIRECTORY "${scratch}/${case_name}")
    if(MSVC)
        set(language_flags /std:c++latest /EHsc)
        set(force_flag /DFORGE_FORCE_HIVE_BACKPORT=1)
        set(include_flags
            "/I${backport_root}"
            "/I${FORGE_ROOT_DIR}/include"
            "/I${RUNTIME_INCLUDE_DIR}")
        set(output_flags "/Fe${executable}")
    else()
        set(language_flags "-std=c++${CXX_STANDARD}")
        set(force_flag -DFORGE_FORCE_HIVE_BACKPORT=1)
        set(include_flags
            "-I${backport_root}"
            "-I${FORGE_ROOT_DIR}/include"
            "-I${RUNTIME_INCLUDE_DIR}")
        set(output_flags -o "${executable}")
    endif()
    execute_process(
        COMMAND "${CXX_COMPILER}" ${language_flags} ${extra_flags} ${force_flag}
            ${include_flags} "${MODEL_SOURCE}" ${output_flags}
        WORKING_DIRECTORY "${scratch}/${case_name}"
        RESULT_VARIABLE compile_result
        OUTPUT_VARIABLE compile_output
        ERROR_VARIABLE compile_error)
    set(log "${scratch}/${case_name}/compile.log")
    file(WRITE "${log}" "${compile_output}\n${compile_error}")
    if(NOT compile_result EQUAL 0)
        message(FATAL_ERROR
            "hive mutation: ${case_name} model compilation failed; see ${log}")
    endif()
    set(${executable_out} "${executable}" PARENT_SCOPE)
endfunction()

function(hive_run_model case_name executable)
    execute_process(
        COMMAND "${executable}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_output
        ERROR_VARIABLE run_error)
    set(log "${scratch}/${case_name}/run.log")
    file(WRITE "${log}" "${run_output}\n${run_error}")
    set(run_result "${run_result}" PARENT_SCOPE)
    set(run_log "${log}" PARENT_SCOPE)
    set(run_text "${run_output}\n${run_error}" PARENT_SCOPE)
endfunction()

hive_compile_model(normal "${normal_backport}" normal_executable)
hive_run_model(normal "${normal_executable}")
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR
        "hive mutation: normal positive control failed; see ${run_log}\n${run_text}")
endif()

hive_compile_model(mutated "${mutant_backport}" mutated_executable)
hive_run_model(mutated "${mutated_executable}")
if(run_result EQUAL 0)
    message(FATAL_ERROR
        "hive mutation: corrupted implementation escaped the independent model; see ${run_log}")
endif()
if(NOT run_text MATCHES "model mismatch:")
    message(FATAL_ERROR
        "hive mutation: mutated runner did not fail through a model mismatch; see ${run_log}\n${run_text}")
endif()
if(NOT run_text MATCHES "size|distance|live model|identity|iteration")
    message(FATAL_ERROR
        "hive mutation: failure was not a relevant size/liveness/iteration mismatch; see ${run_log}")
endif()

file(SHA256 "${source_header}" final_hash)
if(NOT original_hash STREQUAL final_hash)
    message(FATAL_ERROR "hive mutation: source implementation header was modified")
endif()
message(STATUS
    "hive mutation proof: normal model control passed; copied size mutation was rejected")
