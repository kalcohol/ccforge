# Keep the consumer usage requirements; only the mutant overrides Hive itself.
set(mutant_include "${CMAKE_CURRENT_BINARY_DIR}/mutant-include")
file(MAKE_DIRECTORY "${mutant_include}/cpp26")
# Do not include_next into the public wrapper: that would load the unmodified
# implementation before the mutant and invalidate this negative control.
file(WRITE "${mutant_include}/hive" "#pragma once\n#include \"cpp26/hive.hpp\"\n")
file(READ "${hive_source_backport}/cpp26/hive.hpp" implementation)
set(anchor "    control->capacity += block->capacity;\n    ++control->size;")
set(replacement "    control->capacity += block->capacity;\n    control->size += 2;")
string(FIND "${implementation}" "${anchor}" first)
if(first EQUAL -1)
    message(FATAL_ERROR "Hive mutation anchor is absent; update the control explicitly")
endif()
string(LENGTH "${anchor}" length)
math(EXPR end "${first} + ${length}")
string(SUBSTRING "${implementation}" ${end} -1 remaining)
string(FIND "${remaining}" "${anchor}" second)
if(NOT second EQUAL -1)
    message(FATAL_ERROR "Hive mutation anchor is ambiguous")
endif()
string(REPLACE "${anchor}" "${replacement}" mutant "${implementation}")
file(WRITE "${mutant_include}/cpp26/hive.hpp" "${mutant}")
configure_file("${hive_source_backport}/cpp26/hive/detail/bounded_sort.hpp"
    "${mutant_include}/cpp26/hive/detail/bounded_sort.hpp" COPYONLY)

foreach(variant IN ITEMS normal mutated)
    add_executable(hive_model_${variant} "${CMAKE_CURRENT_LIST_DIR}/../mutation/model_main.cpp")
    target_link_libraries(hive_model_${variant} PRIVATE ${hive_test_library})
    if(variant STREQUAL "mutated")
        target_include_directories(hive_model_${variant} BEFORE PRIVATE "${mutant_include}")
    endif()
    target_include_directories(hive_model_${variant} PRIVATE "${hive_runtime_dir}")
endforeach()
add_test(NAME hive_model_positive COMMAND hive_model_normal)
add_test(NAME hive_model_mutation COMMAND "${CMAKE_COMMAND}"
    "-DPROGRAM=$<TARGET_FILE:hive_model_mutated>"
    "-DLOG=${CMAKE_CURRENT_BINARY_DIR}/mutation-run.log"
    -P "${CMAKE_CURRENT_LIST_DIR}/ExpectModelFailure.cmake")
set_tests_properties(hive_model_positive hive_model_mutation PROPERTIES
    TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
set_tests_properties(hive_model_positive PROPERTIES FIXTURES_SETUP hive_model_control)
set_tests_properties(hive_model_mutation PROPERTIES FIXTURES_REQUIRED hive_model_control)
