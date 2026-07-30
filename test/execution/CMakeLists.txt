set(FORGE_EXECUTION_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_EXECUTION_RUNTIME_DIR "${FORGE_EXECUTION_TEST_ROOT}/runtime")

function(forge_add_execution_runtime_test target_name source_name test_name)
    forge_add_runtime_test(
        ${target_name}
        "${FORGE_EXECUTION_RUNTIME_DIR}/${source_name}"
        ${test_name})
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
    target_include_directories(${target_name} BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
endfunction()

function(forge_add_execution_compile_probe target_name source_name test_name)
    forge_add_compile_probe(
        ${target_name}
        "${FORGE_EXECUTION_TEST_ROOT}/compile/${source_name}"
        ${test_name})
    target_include_directories(${target_name} BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
endfunction()

function(forge_expect_execution_cxx_source_compiles result_var source_name should_compile failure_message)
    set(_FORGE_EXECUTION_SAVED_CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
    set(_FORGE_EXECUTION_SAVED_CMAKE_REQUIRED_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS}")
    set(CMAKE_REQUIRED_INCLUDES
        "${CMAKE_SOURCE_DIR}/backport"
        "${CMAKE_SOURCE_DIR}/include")
    foreach(_forge_required_target IN ITEMS forge_std forge)
        if(TARGET ${_forge_required_target})
            get_target_property(
                _forge_target_definitions
                ${_forge_required_target}
                INTERFACE_COMPILE_DEFINITIONS)
            if(_forge_target_definitions)
                foreach(_forge_definition IN LISTS _forge_target_definitions)
                    list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D${_forge_definition}")
                endforeach()
            endif()
        endif()
    endforeach()
    forge_expect_cxx_source_compiles(
        ${result_var}
        "${FORGE_EXECUTION_TEST_ROOT}/compile/${source_name}"
        ${should_compile}
        "${failure_message}")
    set(CMAKE_REQUIRED_INCLUDES "${_FORGE_EXECUTION_SAVED_CMAKE_REQUIRED_INCLUDES}")
    set(CMAKE_REQUIRED_DEFINITIONS "${_FORGE_EXECUTION_SAVED_CMAKE_REQUIRED_DEFINITIONS}")
    unset(_forge_target_definitions)
endfunction()

forge_add_execution_compile_probe(
    test_execution_api_core
    test_execution_api_core.cpp
    execution_api_core_compile)
forge_add_execution_compile_probe(
    test_execution_include_continues_on
    test_execution_include_continues_on.cpp
    execution_include_continues_on_compile)
forge_add_execution_compile_probe(
    test_execution_include_into_variant
    test_execution_include_into_variant.cpp
    execution_include_into_variant_compile)
forge_add_execution_compile_probe(
    test_execution_include_when_all
    test_execution_include_when_all.cpp
    execution_include_when_all_compile)

forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_ACCEPTS_BOUNDARY_PROBES
    test_execution_accept_boundary_probes.cpp
    ON
    "std::execution positive boundary compile probes must compile")

forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_THROWING_RECEIVER
    test_execution_reject_throwing_receiver.cpp
    OFF
    "std::execution receiver completion callbacks must reject throwing set_value")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_THROWING_ERROR_RECEIVER
    test_execution_reject_throwing_error_receiver.cpp
    OFF
    "std::execution receiver completion callbacks must reject throwing set_error")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_THROWING_STOPPED_RECEIVER
    test_execution_reject_throwing_stopped_receiver.cpp
    OFF
    "std::execution receiver completion callbacks must reject throwing set_stopped")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_MOVE_ONLY_LVALUE_SENDER
    test_execution_reject_move_only_lvalue_sender.cpp
    OFF
    "std::execution standard-shaped adaptors must reject non-copyable lvalue senders")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_MOVABLE_OPERATION_STATE
    test_execution_reject_movable_operation_state.cpp
    OFF
    "std::execution operation states must be immovable")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_SPAWN_ERROR_SENDER
    test_execution_reject_spawn_error_sender.cpp
    OFF
    "std::execution::spawn must reject error-completing senders")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_EMPTY_WHEN_ALL
    test_execution_reject_empty_when_all.cpp
    OFF
    "std::execution::when_all must reject zero senders")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_CONTINUES_ON_MULTI_VALUE
    test_execution_reject_continues_on_multi_value.cpp
    OFF
    "std::execution::continues_on subset must reject multiple value completion shapes")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_MISMATCHED_UPON_ERROR
    test_execution_reject_mismatched_upon_error.cpp
    OFF
    "std::execution::upon_error must reject handlers that cannot accept every error shape")

forge_add_execution_runtime_test(test_execution_mvp test_execution_mvp.cpp execution_mvp)
forge_add_execution_runtime_test(
    test_execution_member_customization
    test_execution_member_customization.cpp
    execution_member_customization)
forge_add_execution_runtime_test(
    test_execution_inline_scheduler
    test_execution_inline_scheduler.cpp
    execution_inline_scheduler)
forge_add_execution_runtime_test(test_execution_stop_token test_execution_stop_token.cpp execution_stop_token)
forge_add_execution_runtime_test(test_execution_run_loop test_execution_run_loop.cpp execution_run_loop)
forge_add_execution_runtime_test(test_execution_adaptors test_execution_adaptors.cpp execution_adaptors)
forge_add_execution_runtime_test(test_execution_bulk test_execution_bulk.cpp execution_bulk)
forge_add_execution_runtime_test(test_execution_write_env test_execution_write_env.cpp execution_write_env)
forge_add_execution_runtime_test(test_execution_wave1 test_execution_wave1.cpp execution_wave1)
forge_add_execution_runtime_test(test_execution_wave2 test_execution_wave2.cpp execution_wave2)
forge_add_execution_runtime_test(test_execution_when_all test_execution_when_all.cpp execution_when_all)
forge_add_execution_runtime_test(test_execution_wave3 test_execution_wave3.cpp execution_wave3)
forge_add_execution_runtime_test(test_execution_domain test_execution_domain.cpp execution_domain)
forge_add_execution_runtime_test(
    test_execution_counting_scope
    test_execution_counting_scope.cpp
    execution_counting_scope)
forge_add_execution_runtime_test(
    test_execution_counting_scope_stress
    test_execution_counting_scope_stress.cpp
    execution_counting_scope_stress)
forge_add_execution_runtime_test(
    test_execution_spawn_future
    test_execution_spawn_future.cpp
    execution_spawn_future)
forge_add_execution_runtime_test(test_execution_any_sender test_execution_any_sender.cpp execution_any_sender)
