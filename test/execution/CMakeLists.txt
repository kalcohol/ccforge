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
endfunction()

function(forge_add_execution_compile_probe target_name source_name test_name)
    forge_add_compile_probe(
        ${target_name}
        "${FORGE_EXECUTION_TEST_ROOT}/compile/${source_name}"
        ${test_name})
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
endfunction()

function(forge_expect_execution_cxx_source_compiles result_var source_name should_compile failure_message)
    set(_FORGE_EXECUTION_SAVED_CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
    set(_FORGE_EXECUTION_SAVED_CMAKE_REQUIRED_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS}")
    set(CMAKE_REQUIRED_INCLUDES
        "${FORGE_INCLUDE_DIR}"
        "${FORGE_EXECUTION_TEST_ROOT}/compile")
    if(FORGE_NEEDS_BACKPORT)
        list(PREPEND CMAKE_REQUIRED_INCLUDES "${FORGE_BACKPORT_DIR}")
    endif()
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

# Query and domain constraints, including forwarding_query's two hard Mandates.
forge_add_execution_compile_probe(test_execution_query_conformance
    test_execution_query_conformance.cpp execution_query_conformance_compile)
forge_add_execution_runtime_test(test_execution_query_conformance_runtime
    test_execution_query_conformance.cpp execution_query_conformance)
forge_add_execution_compile_probe(test_execution_env_queries
    test_execution_env_queries.cpp execution_env_queries_compile)
forge_add_execution_runtime_test(test_execution_scheduler_env_queries
    test_execution_scheduler_env_queries.cpp execution_scheduler_env_queries)
forge_add_execution_compile_probe(test_execution_member_dispatch
    test_execution_member_dispatch.cpp execution_member_dispatch_compile)
if(MSVC)
    foreach(_target IN ITEMS test_execution_query_conformance test_execution_query_conformance_runtime
            test_execution_env_queries test_execution_scheduler_env_queries test_execution_member_dispatch)
        target_compile_options(${_target} PRIVATE /bigobj)
    endforeach()
endif()

set(_forge_query_probe_defs "${CMAKE_REQUIRED_DEFINITIONS}")
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_QUERY_MEMBER_POSITIVE
    test_execution_reject_query_mandates.cpp ON "Conforming query control must compile")
foreach(_case IN ITEMS 10 11 21 23 30 32 33 34 35 36 37 38 39)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_query_probe_defs}
        -DFORGE_QUERY_CASE=${_case} -DFORGE_QUERY_POSITIVE=1)
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_QUERY_MEMBER_CONTROL_${_case}
        test_execution_reject_query_mandates.cpp ON "Query Mandate positive control must compile and link")
endforeach()
foreach(_case RANGE 1 39)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_query_probe_defs} -DFORGE_QUERY_CASE=${_case})
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_QUERY_MEMBER_MANDATE_${_case}
        test_execution_reject_query_mandates.cpp OFF "Query and core CPO Mandates must reject this case")
endforeach()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_query_probe_defs}")
unset(_forge_query_probe_defs)
unset(_case)

forge_add_execution_compile_probe(
    test_execution_api_core
    test_execution_api_core.cpp
    execution_api_core_compile)
forge_add_execution_compile_probe(
    test_execution_query_subsumption
    test_execution_query_subsumption.cpp
    execution_query_subsumption_compile)
forge_add_execution_compile_probe(
    test_execution_counting_scope_limits
    test_execution_counting_scope_limits.cpp
    execution_counting_scope_limits_compile)
forge_add_execution_compile_probe(
    test_execution_just_constraints
    test_execution_just_constraints.cpp
    execution_just_constraints_compile)
forge_add_execution_compile_probe(
    test_execution_stopped_optional_constraints
    test_execution_stopped_optional_constraints.cpp
    execution_stopped_optional_constraints_compile)
forge_add_execution_compile_probe(
    test_execution_closure_cvref_constraints
    test_execution_closure_cvref_constraints.cpp
    execution_closure_cvref_constraints_compile)
forge_add_execution_compile_probe(
    test_execution_forwarding_attrs_constraints
    test_execution_forwarding_attrs_constraints.cpp
    execution_forwarding_attrs_constraints_compile)
forge_add_execution_compile_probe(
    test_execution_on_closure_constraints
    test_execution_on_closure_constraints.cpp
    execution_on_closure_constraints_compile)
forge_add_execution_compile_probe(
    test_execution_transfer_signatures
    test_execution_accept_continues_on_multi_value.cpp
    execution_transfer_signatures_compile)
forge_add_execution_compile_probe(
    test_execution_associate_closure_constraints
    test_execution_associate_closure_constraints.cpp
    execution_associate_closure_constraints_compile)
forge_add_execution_compile_probe(
    test_execution_composition_constraints
    test_execution_composition_constraints.cpp
    execution_composition_constraints_compile)
forge_add_execution_compile_probe(
    test_execution_sender_adaptor_closure_constraints
    test_execution_sender_adaptor_closure_constraints.cpp
    execution_sender_adaptor_closure_constraints_compile)
forge_add_execution_compile_probe(
    test_execution_bulk_policy_constraints
    test_execution_bulk_policy_constraints.cpp
    execution_bulk_policy_constraints_compile)
forge_add_execution_compile_probe(
    test_execution_initial_capture_constraints
    test_execution_initial_capture_constraints.cpp
    execution_initial_capture_constraints_compile)
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

file(GLOB _forge_execution_fragment_headers CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/backport/cpp26/execution/*.hpp")
set(_forge_execution_fragment_sources)
set(_forge_execution_fragment_dir
    "${CMAKE_CURRENT_BINARY_DIR}/fragment_includes")
file(MAKE_DIRECTORY "${_forge_execution_fragment_dir}")
foreach(_forge_execution_fragment IN LISTS _forge_execution_fragment_headers)
    get_filename_component(
        _forge_execution_fragment_name
        "${_forge_execution_fragment}"
        NAME)
    set(_forge_execution_fragment_source
        "${_forge_execution_fragment_dir}/${_forge_execution_fragment_name}.cpp")
    file(WRITE "${_forge_execution_fragment_source}"
        "#include \"cpp26/execution/${_forge_execution_fragment_name}\"\n")
    list(APPEND
        _forge_execution_fragment_sources
        "${_forge_execution_fragment_source}")
endforeach()
add_library(
    test_execution_fragment_includes
    OBJECT
    EXCLUDE_FROM_ALL
    ${_forge_execution_fragment_sources})
target_link_libraries(
    test_execution_fragment_includes PRIVATE
    forge::forge)
add_test(
    NAME execution_fragment_includes_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
        ${FORGE_TEST_BUILD_CONFIG_ARGS}
        --target test_execution_fragment_includes)
set_tests_properties(
    execution_fragment_includes_compile PROPERTIES
    RESOURCE_LOCK forge_main_build
    TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
unset(_forge_execution_fragment_headers)
unset(_forge_execution_fragment_sources)
unset(_forge_execution_fragment_dir)
unset(_forge_execution_fragment)
unset(_forge_execution_fragment_name)
unset(_forge_execution_fragment_source)

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
    FORGE_EXECUTION_ACCEPTS_CONTINUES_ON_MULTI_VALUE
    test_execution_accept_continues_on_multi_value.cpp
    ON
    "std::execution::continues_on must preserve multiple value completion shapes")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_MISMATCHED_UPON_ERROR
    test_execution_reject_mismatched_upon_error.cpp
    OFF
    "std::execution::upon_error must reject handlers that cannot accept every error shape")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_ACCEPTS_STOPPED_OPTIONAL_CONSTRAINTS
    test_execution_stopped_optional_constraints.cpp
    ON
    "std::execution::stopped_as_optional positive and constraint probes must compile")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_STOPPED_OPTIONAL_NO_VALUE
    test_execution_reject_stopped_optional_no_value.cpp
    OFF
    "std::execution::stopped_as_optional signature queries must reject senders without value signatures")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_STOPPED_OPTIONAL_EMPTY_VALUE
    test_execution_reject_stopped_optional_empty_value.cpp
    OFF
    "std::execution::stopped_as_optional signature queries must reject void value signatures")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_STOPPED_OPTIONAL_MULTI_VALUE
    test_execution_reject_stopped_optional_multi_value.cpp
    OFF
    "std::execution::stopped_as_optional signature queries must reject multiple value signatures even with the same decay")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_STOPPED_OPTIONAL_DEPENDENT_SIGNATURES
    test_execution_reject_stopped_optional_dependent_signatures.cpp
    OFF
    "std::execution::stopped_as_optional must check dependent completion signatures in their environment")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_STOPPED_OPTIONAL_DEPENDENT_CONNECT
    test_execution_reject_stopped_optional_dependent_connect.cpp
    OFF
    "std::execution::stopped_as_optional rvalue connect must reject a dependent sender without values")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_REJECTS_STOPPED_OPTIONAL_DEPENDENT_CONST_CONNECT
    test_execution_reject_stopped_optional_dependent_const_connect.cpp
    OFF
    "std::execution::stopped_as_optional const connect must reject multiple dependent value signatures")

forge_add_execution_runtime_test(test_execution_mvp test_execution_mvp.cpp execution_mvp)
forge_add_execution_runtime_test(
    test_execution_just
    test_execution_just.cpp
    execution_just)
forge_add_execution_runtime_test(
    test_execution_stopped_optional
    test_execution_stopped_optional.cpp
    execution_stopped_optional)
forge_add_execution_runtime_test(
    test_execution_closure_cvref
    test_execution_closure_cvref.cpp
    execution_closure_cvref)
forge_add_execution_runtime_test(
    test_execution_forwarding_attrs
    test_execution_forwarding_attrs.cpp
    execution_forwarding_attrs)
forge_add_execution_runtime_test(
    test_execution_on_closure
    test_execution_on_closure.cpp
    execution_on_closure)
forge_add_execution_runtime_test(
    test_execution_associate_closure
    test_execution_associate_closure.cpp
    execution_associate_closure)
forge_add_execution_runtime_test(
    test_execution_composition
    test_execution_composition.cpp
    execution_composition)
forge_add_execution_runtime_test(
    test_execution_sender_adaptor_closure
    test_execution_sender_adaptor_closure.cpp
    execution_sender_adaptor_closure)
if(MSVC)
    target_compile_options(test_execution_sender_adaptor_closure PRIVATE /bigobj)
    target_compile_options(test_execution_sender_adaptor_closure_constraints PRIVATE /bigobj)
endif()
forge_add_execution_runtime_test(
    test_execution_member_customization
    test_execution_member_customization.cpp
    execution_member_customization)
forge_add_execution_runtime_test(
    test_execution_inline_scheduler
    test_execution_inline_scheduler.cpp
    execution_inline_scheduler)
forge_add_execution_runtime_test(test_execution_stop_token test_execution_stop_token.cpp execution_stop_token)
forge_add_execution_runtime_test(test_stop_intrusive
    test_execution_stop_intrusive.cpp execution_stop_intrusive)
target_compile_definitions(test_stop_intrusive PRIVATE
    FORGE_EXECUTION_TEST_STOP_HOOKS=1)
set_tests_properties(execution_stop_intrusive PROPERTIES TIMEOUT 180)

# Forced-parking and conditional-notification tests for inplace_stop_source.
# Both targets need the test-only hooks; the strict one adds the lifetime probes.
forge_add_execution_runtime_test(test_stop_wait
    test_execution_stop_wait.cpp execution_stop_wait)
target_compile_definitions(test_stop_wait PRIVATE
    FORGE_EXECUTION_TEST_STOP_HOOKS=1)
forge_add_execution_runtime_test(test_stop_wait_strict
    test_execution_stop_wait.cpp execution_stop_wait_strict)
target_compile_definitions(test_stop_wait_strict PRIVATE
    FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME=1
    FORGE_EXECUTION_TEST_STOP_HOOKS=1)
set_tests_properties(execution_stop_wait execution_stop_wait_strict PROPERTIES TIMEOUT 600)
forge_add_execution_runtime_test(test_execution_stop_callback_lifetime
    test_execution_stop_callback_lifetime.cpp execution_stop_callback_lifetime)

# This macro belongs exclusively to test executables; never export it from a
# library target or set it as a production configuration option.
forge_add_execution_runtime_test(test_execution_stop_callback_strict_lifetime
    test_execution_stop_callback_lifetime.cpp execution_stop_callback_strict_lifetime)
target_sources(test_execution_stop_callback_strict_lifetime PRIVATE
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_stop_intrusive.cpp"
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_stop_token.cpp"
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_when_all.cpp"
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_counting_scope.cpp"
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_counting_scope_lifetime.cpp"
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_counting_scope_stress.cpp"
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_spawn_future.cpp")
target_compile_definitions(test_execution_stop_callback_strict_lifetime PRIVATE
    FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME=1
    FORGE_EXECUTION_TEST_STOP_HOOKS=1)
forge_add_execution_runtime_test(test_execution_stop_callback_allocations
    test_execution_stop_callback_allocations.cpp execution_stop_callback_allocations)
target_compile_definitions(test_execution_stop_callback_allocations PRIVATE
    FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME=1)
set_tests_properties(execution_stop_callback_allocations PROPERTIES SKIP_RETURN_CODE 77)
if(MSVC)
    target_compile_options(test_stop_wait PRIVATE /bigobj)
    target_compile_options(test_stop_wait_strict PRIVATE /bigobj)
    target_compile_options(test_execution_stop_callback_lifetime PRIVATE /bigobj)
    target_compile_options(test_execution_stop_callback_strict_lifetime PRIVATE /bigobj)
endif()
add_custom_target(test_stop_callback_strict_lifetime
    DEPENDS test_stop_intrusive test_stop_wait test_stop_wait_strict test_execution_stop_callback_strict_lifetime test_execution_stop_callback_allocations)
forge_add_execution_runtime_test(test_execution_run_loop test_execution_run_loop.cpp execution_run_loop)
forge_add_execution_runtime_test(test_execution_adaptors test_execution_adaptors.cpp execution_adaptors)
forge_add_execution_runtime_test(
    test_execution_static_opstate_allocations
    test_execution_static_opstate_allocations.cpp
    execution_static_opstate_allocations)
set_tests_properties(execution_static_opstate_allocations PROPERTIES
    SKIP_RETURN_CODE 77)
forge_add_execution_runtime_test(
    test_execution_static_opstate_transfer
    test_execution_static_opstate_transfer.cpp
    execution_static_opstate_transfer)
set_tests_properties(execution_static_opstate_transfer PROPERTIES
    SKIP_RETURN_CODE 77)
forge_add_execution_runtime_test(
    test_execution_static_opstate_scope
    test_execution_static_opstate_scope.cpp
    execution_static_opstate_scope)
set_tests_properties(execution_static_opstate_scope PROPERTIES
    SKIP_RETURN_CODE 77)
forge_add_execution_runtime_test(
    test_execution_static_opstate_exceptions
    test_execution_static_opstate_exceptions.cpp
    execution_static_opstate_exceptions)
if(MSVC)
    # Nested compositions can exceed the regular COFF section limit in debug builds.
    target_compile_options(test_execution_static_opstate_allocations PRIVATE /bigobj)
    target_compile_options(test_execution_static_opstate_exceptions PRIVATE /bigobj)
    target_compile_options(test_execution_static_opstate_transfer PRIVATE /bigobj)
    target_compile_options(test_execution_static_opstate_scope PRIVATE /bigobj)
endif()
forge_add_execution_runtime_test(
    test_execution_throwing_move_adaptors
    test_execution_throwing_move_adaptors.cpp
    execution_throwing_move_adaptors)
forge_add_execution_runtime_test(test_execution_bulk test_execution_bulk.cpp execution_bulk)
forge_add_execution_runtime_test(test_execution_bulk_policy test_execution_bulk_policy.cpp execution_bulk_policy)
forge_add_execution_runtime_test(test_execution_initial_capture test_execution_initial_capture.cpp execution_initial_capture)
forge_add_execution_runtime_test(test_execution_write_env test_execution_write_env.cpp execution_write_env)
forge_add_execution_runtime_test(
    test_execution_borrowed_env test_execution_borrowed_env.cpp execution_borrowed_env)
forge_add_execution_runtime_test(test_execution_wave1 test_execution_wave1.cpp execution_wave1)
forge_add_execution_runtime_test(test_execution_wave2 test_execution_wave2.cpp execution_wave2)
forge_add_execution_runtime_test(test_execution_when_all test_execution_when_all.cpp execution_when_all)
forge_add_execution_runtime_test(test_execution_wave3 test_execution_wave3.cpp execution_wave3)
forge_add_execution_runtime_test(
    test_execution_schedule_from
    test_execution_schedule_from.cpp
    execution_schedule_from)
forge_add_execution_runtime_test(test_execution_domain test_execution_domain.cpp execution_domain)
forge_add_execution_runtime_test(
    test_execution_counting_scope
    test_execution_counting_scope.cpp
    execution_counting_scope)
forge_add_execution_runtime_test(
    test_execution_counting_scope_limits_runtime
    test_execution_counting_scope_limits.cpp
    execution_counting_scope_limits)
forge_add_execution_runtime_test(
    test_execution_counting_scope_stress
    test_execution_counting_scope_stress.cpp
    execution_counting_scope_stress)
forge_add_execution_runtime_test(
    test_execution_counting_scope_lifetime
    test_execution_counting_scope_lifetime.cpp
    execution_counting_scope_lifetime)
forge_add_execution_runtime_test(
    test_execution_counting_scope_decrement_tail
    test_execution_counting_scope_decrement_tail.cpp
    execution_counting_scope_decrement_tail)
forge_add_execution_runtime_test(
    test_execution_spawn_future
    test_execution_spawn_future.cpp
    execution_spawn_future)
if(MSVC)
    target_compile_options(test_execution_counting_scope PRIVATE /bigobj)
    target_compile_options(test_execution_spawn_future PRIVATE /bigobj)
endif()
forge_add_execution_runtime_test(test_execution_any_sender test_execution_any_sender.cpp execution_any_sender)

forge_add_execution_runtime_test(
    test_execution_extensions
    test_execution_extensions.cpp
    execution_extensions)

# Call the consumer in its body so the Mandates are instantiated.
set(_forge_sync_wait_defs "${CMAKE_REQUIRED_DEFINITIONS}")
forge_expect_execution_cxx_source_compiles(
    FORGE_EXECUTION_ACCEPTS_SINGLE_VALUE_SYNC_WAIT
    test_execution_sync_wait_mandates.cpp ON "The wait consumers must accept the prescribed forms")
foreach(_case RANGE 1 8)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_sync_wait_defs} -DFORGE_SYNC_WAIT_CASE=${_case})
    forge_expect_execution_cxx_source_compiles(
        FORGE_EXECUTION_SYNC_WAIT_MANDATE_${_case}
        test_execution_sync_wait_mandates.cpp OFF "The wait consumer must diagnose Mandate case ${_case}")
endforeach()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_sync_wait_defs}")
unset(_forge_sync_wait_defs)
unset(_case)

set(_forge_extension_defs "${CMAKE_REQUIRED_DEFINITIONS}")
forge_add_execution_compile_probe(test_execution_removed_forms
    test_execution_removed_forms.cpp execution_removed_forms_compile)
forge_add_execution_compile_probe(test_execution_standard_forms
    test_execution_standard_forms.cpp execution_standard_forms_compile)
foreach(_target IN ITEMS test_execution_removed_forms test_execution_standard_forms)
    if(MSVC)
        target_compile_options(${_target} PRIVATE /bigobj)
    endif()
    string(REGEX REPLACE "^test_" "" _test "${_target}")
    add_test(NAME ${_test} COMMAND ${_target})
    set_tests_properties(${_test} PROPERTIES
        DEPENDS ${_test}_compile TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endforeach()
foreach(_case RANGE 1 35)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_extension_defs}
        -DFORGE_REMOVED_NAME=${_case} -DFORGE_REMOVAL_CONTROL=1)
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_REMOVED_NAME_CONTROL_${_case}
        test_execution_reject_removed_names.cpp ON "Control for removed execution name or form ${_case} must compile")
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_extension_defs}
        -DFORGE_REMOVED_NAME=${_case} -DFORGE_REMOVAL_CONTROL=0)
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_REMOVED_NAME_REJECT_${_case}
        test_execution_reject_removed_names.cpp OFF "Removed execution name or form ${_case} must be absent")
endforeach()
unset(_target)
unset(_test)
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_extension_defs}")
unset(_forge_extension_defs)
unset(_forge_case)

# Adaptor signatures, constraints and allocator queries.
set(_forge_adaptor_saved_defs "${CMAKE_REQUIRED_DEFINITIONS}")
forge_add_execution_compile_probe(test_execution_adaptor_conformance
    test_execution_adaptor_conformance.cpp execution_adaptor_conformance_compile)
forge_add_execution_runtime_test(test_execution_adaptor_conformance_runtime
    test_execution_adaptor_conformance.cpp execution_adaptor_conformance)
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_adaptor_saved_defs})
foreach(_case RANGE 1 9)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_adaptor_saved_defs}
        -DFORGE_ADAPTOR_CASE=${_case} -DFORGE_ADAPTOR_POSITIVE=1)
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_ADAPTOR_CONTROL_MEMBER_${_case}
        test_execution_reject_adaptor_constraints.cpp ON "Adaptor positive control must compile and link")
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_adaptor_saved_defs} -DFORGE_ADAPTOR_CASE=${_case})
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_ADAPTOR_REJECT_MEMBER_${_case}
        test_execution_reject_adaptor_constraints.cpp OFF "Adaptor creation constraint must reject this case")
endforeach()
if(MSVC)
    target_compile_options(test_execution_adaptor_conformance PRIVATE /bigobj)
    target_compile_options(test_execution_adaptor_conformance_runtime PRIVATE /bigobj)
endif()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_adaptor_saved_defs}")
unset(_forge_adaptor_saved_defs)

# Scheduling attributes and affine use receiver-dependent query contracts.
set(_forge_scheduling_saved_defs "${CMAKE_REQUIRED_DEFINITIONS}")
forge_add_execution_compile_probe(test_execution_scheduling_conformance
    test_execution_scheduling_conformance.cpp execution_scheduling_conformance_compile)
forge_add_execution_runtime_test(test_execution_scheduling_conformance_runtime
    test_execution_scheduling_conformance.cpp execution_scheduling_conformance)
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_scheduling_saved_defs})
foreach(_scheduler RANGE 0 1)
    foreach(_token RANGE 0 1)
        set(CMAKE_REQUIRED_DEFINITIONS ${_forge_scheduling_saved_defs}
            -DFORGE_AFFINE_FORGE_SCHEDULER=${_scheduler} -DFORGE_AFFINE_FORGE_TOKEN=${_token}
            -DFORGE_ADAPTOR_POSITIVE=1)
        forge_expect_execution_cxx_source_compiles(FORGE_AFFINE_FORGE_CONTROL_MEMBER_${_scheduler}_${_token}
            test_execution_reject_affine_forge.cpp ON "Stoppable Forge affine control must compile and link")
        set(CMAKE_REQUIRED_DEFINITIONS ${_forge_scheduling_saved_defs}
            -DFORGE_AFFINE_FORGE_SCHEDULER=${_scheduler} -DFORGE_AFFINE_FORGE_TOKEN=${_token})
        forge_expect_execution_cxx_source_compiles(FORGE_AFFINE_FORGE_REJECT_MEMBER_${_scheduler}_${_token}
            test_execution_reject_affine_forge.cpp OFF "Unstoppable Forge affine connection must be unavailable")
    endforeach()
endforeach()
if(MSVC)
    target_compile_options(test_execution_scheduling_conformance PRIVATE /bigobj)
    target_compile_options(test_execution_scheduling_conformance_runtime PRIVATE /bigobj)
endif()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_scheduling_saved_defs}")
unset(_forge_scheduling_saved_defs)
unset(_scheduler)
unset(_token)

# Replacing a Forge predecessor in let destroys it during its own completion.
# Both sanitizer lanes build execution tests and select the execution prefix.
forge_add_execution_runtime_test(test_execution_forge_predecessor_lifetime
    test_execution_forge_predecessor_lifetime.cpp execution_forge_predecessor_lifetime)
if(MSVC)
    target_compile_options(test_execution_forge_predecessor_lifetime PRIVATE /bigobj)
endif()

# Awaitable signatures, coroutine ownership and Mandates.
# The execution prefix includes the frame self-destruction tests in both sanitizer lanes.
set(_forge_awaitable_saved_defs "${CMAKE_REQUIRED_DEFINITIONS}")
forge_add_execution_compile_probe(test_execution_awaitable_conformance
    test_execution_awaitable_conformance.cpp execution_awaitable_conformance_compile)
forge_add_execution_runtime_test(test_execution_awaitable_conformance_runtime
    test_execution_awaitable_conformance.cpp execution_awaitable_conformance)
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_awaitable_saved_defs})
foreach(_case RANGE 1 9)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_awaitable_saved_defs}
        -DFORGE_AWAITABLE_CASE=${_case} -DFORGE_AWAITABLE_POSITIVE=1)
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_AWAITABLE_CONTROL_MEMBER_${_case}
        test_execution_reject_awaitable.cpp ON "Awaitable positive control must compile and link")
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_awaitable_saved_defs} -DFORGE_AWAITABLE_CASE=${_case})
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_AWAITABLE_REJECT_MEMBER_${_case}
        test_execution_reject_awaitable.cpp OFF "Awaitable Mandate or constraint must reject this case")
endforeach()
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_awaitable_saved_defs}
    -DFORGE_AWAITABLE_CASE=10 -DFORGE_AWAITABLE_POSITIVE=1)
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_AWAITABLE_RVALUE_BRIDGE_CONTROL
    test_execution_reject_awaitable.cpp ON "An rvalue sender must be accepted by the sender bridge")
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_awaitable_saved_defs} -DFORGE_AWAITABLE_CASE=10)
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_AWAITABLE_REJECT_LVALUE_BRIDGE
    test_execution_reject_awaitable.cpp OFF "The sender bridge must not decay-copy an lvalue sender")
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_awaitable_saved_defs}
    -DFORGE_AWAITABLE_CASE=11 -DFORGE_AWAITABLE_POSITIVE=1)
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_AWAITABLE_ADAPTED_VALUE_BRIDGE_CONTROL
    test_execution_reject_awaitable.cpp ON "The sender bridge must accept a single-value sender returned by value")
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_awaitable_saved_defs} -DFORGE_AWAITABLE_CASE=11)
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_AWAITABLE_REJECT_ADAPTED_LVALUE_BRIDGE
    test_execution_reject_awaitable.cpp OFF "The sender bridge must reject a single-value sender returned by lvalue reference")
if(MSVC)
    target_compile_options(test_execution_awaitable_conformance PRIVATE /bigobj)
    target_compile_options(test_execution_awaitable_conformance_runtime PRIVATE /bigobj)
endif()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_awaitable_saved_defs}")
unset(_forge_awaitable_saved_defs)


# The execution prefix selects this runtime test in both sanitizer lanes.
forge_add_execution_runtime_test(test_execution_forge_bulk
    test_execution_forge_bulk.cpp execution_forge_bulk)
