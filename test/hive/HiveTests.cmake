# Shared by the public-target integration and the isolated standalone harness.
set(hive_compile_dir "${CMAKE_CURRENT_LIST_DIR}/compile")
set(hive_runtime_dir "${CMAKE_CURRENT_LIST_DIR}/runtime")

include("${CMAKE_CURRENT_LIST_DIR}/PortableTests.cmake")

# Each rejection has a corresponding positive control compiled first.
hive_compile_check(include TRUE)
hive_compile_check(api TRUE)
hive_compile_check(constexpr_allocator TRUE)
hive_compile_check(version_then_include TRUE)

add_executable(hive_runtime
    "${hive_runtime_dir}/test_hive_alloc_layout.cpp"
    "${hive_runtime_dir}/test_hive_allocators.cpp"
    "${hive_runtime_dir}/test_hive_capacity.cpp"
    "${hive_runtime_dir}/test_hive_contract_controls.cpp"
    "${hive_runtime_dir}/test_hive_growth.cpp"
    "${hive_runtime_dir}/test_hive_get_iterator.cpp"
    "${hive_runtime_dir}/test_hive_iteration.cpp"
    "${hive_runtime_dir}/test_hive_layout.cpp"
    "${hive_runtime_dir}/test_hive_ranges_types.cpp"
    "${hive_runtime_dir}/test_hive_skipfield.cpp"
    "${hive_runtime_dir}/test_hive_exceptions.cpp"
    "${hive_runtime_dir}/test_hive_fancy_allocator.cpp"
    "${hive_runtime_dir}/test_hive_semantics.cpp"
    "${hive_runtime_dir}/test_hive_sort.cpp"
    "${hive_runtime_dir}/test_hive_sort_r41.cpp"
    "${hive_runtime_dir}/test_hive_splice_edges.cpp")
target_compile_definitions(hive_runtime PRIVATE FORGE_HIVE_TEST_HOOKS=1)
target_link_libraries(hive_runtime PRIVATE GTest::gtest_main ${hive_test_library})
add_test(NAME hive_runtime COMMAND hive_runtime)

# Also check the production layout without diagnostic members.
add_executable(hive_alloc_layout "${hive_runtime_dir}/test_hive_alloc_layout.cpp")
target_link_libraries(hive_alloc_layout PRIVATE GTest::gtest_main ${hive_test_library})
add_test(NAME hive_alloc_layout COMMAND hive_alloc_layout)

add_executable(hive_multi_tu
    "${hive_runtime_dir}/test_hive_multi_tu_main.cpp"
    "${hive_runtime_dir}/test_hive_multi_tu_other.cpp")
target_link_libraries(hive_multi_tu PRIVATE ${hive_test_library})
add_test(NAME hive_multi_tu COMMAND hive_multi_tu)
set_tests_properties(hive_runtime hive_multi_tu hive_alloc_layout PROPERTIES
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
    add_executable(hive_bounded_sort_probe "${CMAKE_CURRENT_LIST_DIR}/benchmark/measure_bounded_sort.cpp")
    set_target_properties(hive_bounded_sort_probe PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    # Measurements may intentionally inspect deprecated standard-library traits.
    set_target_properties(hive_cost_probe hive_traversal_probe hive_sort_probe
        hive_bounded_sort_probe PROPERTIES FORGE_DEPRECATION_STRICT_SKIP TRUE)
endif()

option(FORGE_HIVE_BUILD_STRESS "Build the optional randomized Hive model stress" OFF)
if(FORGE_HIVE_BUILD_STRESS)
    add_executable(hive_stress "${CMAKE_CURRENT_LIST_DIR}/stress/model_stress.cpp")
    target_link_libraries(hive_stress PRIVATE ${hive_test_library})
    target_compile_definitions(hive_stress PRIVATE FORGE_HIVE_TEST_HOOKS=1)
endif()

# Direct-header kernel validation deliberately does not inherit Hive's C++23 target.
add_executable(hive_bounded_sort "${hive_runtime_dir}/test_hive_bounded_sort.cpp")
target_compile_definitions(hive_bounded_sort PRIVATE FORGE_HIVE_TEST_HOOKS=1)
target_link_libraries(hive_bounded_sort PRIVATE GTest::gtest_main)
set_target_properties(hive_bounded_sort PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
add_test(NAME hive_bounded_sort COMMAND hive_bounded_sort)
# Full million-relation and every-throw controls remain enabled in sanitizer lanes.
set_tests_properties(hive_bounded_sort PROPERTIES TIMEOUT 3600 LABELS "hive;bounded_sort;full_controls")
