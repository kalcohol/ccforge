# Only working-draft API and guaranteed semantics; no implementation hooks.
set(hive_compile_dir "${CMAKE_CURRENT_LIST_DIR}/compile")
hive_compile_check(fancy_api TRUE)
hive_compile_check(accept_from_range TRUE)
hive_compile_check(reject_incompatible_from_range FALSE accept_from_range)
hive_compile_check(accept_construct_only_range TRUE)
hive_compile_check(accept_assign_range TRUE)
hive_compile_check(reject_nonassignable_assign_range FALSE accept_assign_range)
hive_compile_check(accept_iterator_allocator TRUE)
hive_compile_check(reject_non_allocator FALSE accept_iterator_allocator)
hive_compile_check(accept_legacy_input TRUE)
hive_compile_check(reject_non_input_iterator FALSE accept_legacy_input)

add_executable(hive_portable
    "${CMAKE_CURRENT_LIST_DIR}/runtime/test_hive_portable.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/test_hive_portable_allocators.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/test_hive_portable_exceptions.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/test_hive_portable_semantics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/test_hive_portable_contract_controls.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/test_hive_portable_iteration.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/test_hive_portable_fancy_allocator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/test_hive_portable_ranges_types.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/test_hive_core_model.cpp")
target_link_libraries(hive_portable PRIVATE GTest::gtest_main ${hive_test_library})
add_test(NAME hive_portable_runtime COMMAND hive_portable)
set_tests_properties(hive_portable_runtime PROPERTIES TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
