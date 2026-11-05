# Shared by the public-target integration and the isolated standalone harness.
set(hive_compile_dir "${CMAKE_CURRENT_LIST_DIR}/compile")
set(hive_runtime_dir "${CMAKE_CURRENT_LIST_DIR}/runtime")

# Each rejection has a corresponding positive control compiled first.
hive_compile_check(include TRUE)
hive_compile_check(api TRUE)
hive_compile_check(fancy_api TRUE)
hive_compile_check(constexpr_allocator TRUE)
hive_compile_check(accept_from_range TRUE)
hive_compile_check(reject_incompatible_from_range FALSE accept_from_range)
hive_compile_check(accept_construct_only_range TRUE)
hive_compile_check(accept_assign_range TRUE)
hive_compile_check(reject_nonassignable_assign_range FALSE accept_assign_range)
hive_compile_check(accept_iterator_allocator TRUE)
hive_compile_check(reject_non_allocator FALSE accept_iterator_allocator)
hive_compile_check(accept_legacy_input TRUE)
hive_compile_check(reject_non_input_iterator FALSE accept_legacy_input)
hive_compile_check(version_then_include TRUE)

add_executable(hive_runtime
    "${hive_runtime_dir}/test_hive_allocators.cpp"
    "${hive_runtime_dir}/test_hive_capacity.cpp"
    "${hive_runtime_dir}/test_hive_contract_controls.cpp"
    "${hive_runtime_dir}/test_hive_growth.cpp"
    "${hive_runtime_dir}/test_hive_get_iterator.cpp"
    "${hive_runtime_dir}/test_hive_iteration.cpp"
    "${hive_runtime_dir}/test_hive_layout.cpp"
    "${hive_runtime_dir}/test_hive_skipfield.cpp"
    "${hive_runtime_dir}/test_hive_core_model.cpp"
    "${hive_runtime_dir}/test_hive_exceptions.cpp"
    "${hive_runtime_dir}/test_hive_fancy_allocator.cpp"
    "${hive_runtime_dir}/test_hive_ranges_types.cpp"
    "${hive_runtime_dir}/test_hive_semantics.cpp"
    "${hive_runtime_dir}/test_hive_splice_edges.cpp")
target_compile_definitions(hive_runtime PRIVATE FORGE_HIVE_TEST_HOOKS=1)
target_link_libraries(hive_runtime PRIVATE GTest::gtest_main ${hive_test_library})
add_test(NAME hive_runtime COMMAND hive_runtime)

add_executable(hive_multi_tu
    "${hive_runtime_dir}/test_hive_multi_tu_main.cpp"
    "${hive_runtime_dir}/test_hive_multi_tu_other.cpp")
target_link_libraries(hive_multi_tu PRIVATE ${hive_test_library})
add_test(NAME hive_multi_tu COMMAND hive_multi_tu)
set_tests_properties(hive_runtime hive_multi_tu PROPERTIES
    TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})

message(STATUS "Hive <version>-only feature macro check: DEFERRED (no shared version backport)")
include("${CMAKE_CURRENT_LIST_DIR}/standalone/Mutation.cmake")

option(FORGE_HIVE_BUILD_COST_PROBE "Build the optional local Hive cost probe" OFF)
if(FORGE_HIVE_BUILD_COST_PROBE)
    add_executable(hive_cost_probe "${CMAKE_CURRENT_LIST_DIR}/benchmark/measure_core.cpp")
    target_link_libraries(hive_cost_probe PRIVATE ${hive_test_library})
    add_executable(hive_traversal_probe "${CMAKE_CURRENT_LIST_DIR}/benchmark/measure_traversal.cpp")
    target_link_libraries(hive_traversal_probe PRIVATE ${hive_test_library})
    add_executable(hive_sort_probe "${CMAKE_CURRENT_LIST_DIR}/benchmark/measure_sort.cpp")
    target_link_libraries(hive_sort_probe PRIVATE ${hive_test_library})
endif()

option(FORGE_HIVE_BUILD_STRESS "Build the optional randomized Hive model stress" OFF)
if(FORGE_HIVE_BUILD_STRESS)
    add_executable(hive_stress "${CMAKE_CURRENT_LIST_DIR}/stress/model_stress.cpp")
    target_link_libraries(hive_stress PRIVATE ${hive_test_library})
    target_compile_definitions(hive_stress PRIVATE FORGE_HIVE_TEST_HOOKS=1)
endif()
