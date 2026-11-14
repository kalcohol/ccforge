# Explicit independent expectations for the main tree's Hive registrations.
# Optional benchmarks and stress targets are disabled by the handoff fixture.
function(hive_expected_registrations state targets_var tests_var)
    set(targets hive_portable
        hive_compile_fancy_api
        hive_compile_accept_from_range
        hive_compile_reject_incompatible_from_range
        hive_compile_accept_construct_only_range
        hive_compile_accept_assign_range
        hive_compile_reject_nonassignable_assign_range
        hive_compile_accept_iterator_allocator
        hive_compile_reject_non_allocator
        hive_compile_accept_legacy_input
        hive_compile_reject_non_input_iterator)
    set(tests ${targets})
    list(REMOVE_ITEM tests hive_portable)
    list(APPEND tests hive_portable_runtime hive_test_gate_matrix)
    if(state STREQUAL "BACKPORT" OR state STREQUAL "FORCED")
        set(implementation_targets hive_runtime hive_multi_tu hive_alloc_layout
            hive_compile_include hive_compile_api hive_compile_constexpr_allocator
            hive_compile_version_then_include hive_model_normal hive_model_mutated
            hive_bounded_sort)
        list(APPEND targets ${implementation_targets})
        list(REMOVE_ITEM implementation_targets hive_model_normal hive_model_mutated)
        list(APPEND tests ${implementation_targets} hive_model_positive hive_model_mutation)
    elseif(NOT state STREQUAL "COMPLETE" AND NOT state STREQUAL "PARTIAL")
        message(FATAL_ERROR "Unknown Hive expectation state: ${state}")
    endif()
    list(SORT targets)
    list(SORT tests)
    set(${targets_var} "${targets}" PARENT_SCOPE)
    set(${tests_var} "${tests}" PARENT_SCOPE)
endfunction()

function(hive_assert_registrations context actual expected)
    list(FILTER actual INCLUDE REGEX "^hive_")
    list(SORT actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${context}: expected ${expected}; got ${actual}")
    endif()
endfunction()
