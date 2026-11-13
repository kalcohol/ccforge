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
        "${CMAKE_SOURCE_DIR}/include"
        "${FORGE_EXECUTION_TEST_ROOT}/compile")
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
    test_execution_env_queries
    test_execution_env_queries.cpp
    execution_env_queries_compile)
forge_add_execution_compile_probe(
    test_execution_strict_env_queries
    test_execution_env_queries.cpp
    execution_strict_env_queries_compile)
target_compile_definitions(test_execution_strict_env_queries PRIVATE
    FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES=1)
if(MSVC)
    target_compile_options(test_execution_env_queries PRIVATE /bigobj)
    target_compile_options(test_execution_strict_env_queries PRIVATE /bigobj)
endif()

foreach(_mode IN ITEMS default strict)
    set(_query_target test_execution_${_mode}_scheduler_env_queries)
    forge_add_execution_runtime_test(${_query_target}
        test_execution_scheduler_env_queries.cpp execution_${_mode}_scheduler_env_queries)
    if(_mode STREQUAL "strict")
        target_compile_definitions(${_query_target} PRIVATE FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES=1)
    endif()
    if(MSVC)
        target_compile_options(${_query_target} PRIVATE /bigobj)
    endif()
endforeach()

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
target_include_directories(
    test_execution_fragment_includes BEFORE PRIVATE
    "${CMAKE_SOURCE_DIR}/backport")
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
forge_add_execution_runtime_test(test_execution_stop_callback_lifetime
    test_execution_stop_callback_lifetime.cpp execution_stop_callback_lifetime)

# This macro belongs exclusively to test executables; never export it from a
# library target or set it as a production configuration option.
forge_add_execution_runtime_test(test_execution_stop_callback_strict_lifetime
    test_execution_stop_callback_lifetime.cpp execution_stop_callback_strict_lifetime)
target_sources(test_execution_stop_callback_strict_lifetime PRIVATE
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_stop_token.cpp"
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_when_all.cpp"
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_counting_scope.cpp"
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_counting_scope_lifetime.cpp"
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_counting_scope_stress.cpp"
    "${FORGE_EXECUTION_RUNTIME_DIR}/test_execution_spawn_future.cpp")
target_compile_definitions(test_execution_stop_callback_strict_lifetime PRIVATE
    FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME=1)
# These cases deliberately destroy an executing inline source. Keep the strict
# reproducer available without adding the known ASan failure to the passing gate.
set_tests_properties(execution_stop_callback_strict_lifetime PROPERTIES
    ENVIRONMENT "GTEST_FILTER=-*KnownOpenTest.*")
add_custom_target(test_execution_stop_callback_known_open
    COMMAND $<TARGET_FILE:test_execution_stop_callback_strict_lifetime>
        "--gtest_filter=*KnownOpenTest.*"
    DEPENDS test_execution_stop_callback_strict_lifetime
    USES_TERMINAL VERBATIM)
forge_add_execution_runtime_test(test_execution_stop_callback_allocations
    test_execution_stop_callback_allocations.cpp execution_stop_callback_allocations)
target_compile_definitions(test_execution_stop_callback_allocations PRIVATE
    FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME=1)
set_tests_properties(execution_stop_callback_allocations PROPERTIES SKIP_RETURN_CODE 77)
if(MSVC)
    target_compile_options(test_execution_stop_callback_lifetime PRIVATE /bigobj)
    target_compile_options(test_execution_stop_callback_strict_lifetime PRIVATE /bigobj)
endif()
add_custom_target(test_stop_callback_strict_lifetime
    DEPENDS test_execution_stop_callback_strict_lifetime test_execution_stop_callback_allocations)
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

forge_add_execution_compile_probe(
    test_execution_extension_compat
    test_execution_extension_compat.cpp
    execution_extension_compat_compile)
forge_add_execution_runtime_test(
    test_execution_extensions
    test_execution_extensions.cpp
    execution_extensions)
add_test(NAME execution_extension_compat COMMAND test_execution_extension_compat)
set_tests_properties(execution_extension_compat PROPERTIES
    DEPENDS execution_extension_compat_compile
    TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
foreach(_forge_extension_test IN ITEMS
        test_execution_extensions test_execution_extension_compat)
    if(MSVC)
        target_compile_options(${_forge_extension_test} PRIVATE /we4996)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${_forge_extension_test} PRIVATE -Werror=deprecated-declarations)
    endif()
endforeach()

# Deprecated extension calls remain usable and diagnose independently.
set(_forge_extension_flags "${CMAKE_REQUIRED_FLAGS}")
set(_forge_extension_defs "${CMAKE_REQUIRED_DEFINITIONS}")
if(MSVC)
    string(APPEND CMAKE_REQUIRED_FLAGS " /we4996")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    string(APPEND CMAKE_REQUIRED_FLAGS " -Werror=deprecated-declarations")
endif()
forge_add_execution_compile_probe(test_execution_scope_observer_compat test_execution_scope_observer_compat.cpp execution_scope_observer_compat_compile)
add_test(NAME execution_scope_observer_compat COMMAND test_execution_scope_observer_compat)
set_tests_properties(execution_scope_observer_compat PROPERTIES DEPENDS execution_scope_observer_compat_compile)
if(MSVC)
    target_compile_options(test_execution_scope_observer_compat PRIVATE /we4996)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_execution_scope_observer_compat PRIVATE -Werror=deprecated-declarations)
endif()
if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_SCOPE_OBSERVER_COMPAT
        test_execution_scope_observer_compat.cpp ON "Suppressed legacy extensions and Forge replacements must compile")
    foreach(_forge_case RANGE 1 4)
        set(CMAKE_REQUIRED_DEFINITIONS ${_forge_extension_defs} -DFORGE_DEPRECATION_CASE=${_forge_case})
        forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_SCOPE_OBSERVER_DEPRECATED_${_forge_case}
            test_execution_scope_observer_compat.cpp OFF "Each legacy extension must diagnose deprecation")
    endforeach()
    set(CMAKE_REQUIRED_DEFINITIONS "${_forge_extension_defs}")
endif()
forge_add_execution_compile_probe(test_execution_zero_wait_compat test_execution_zero_wait_compat.cpp execution_zero_wait_compat_compile)
add_test(NAME execution_zero_wait_compat COMMAND test_execution_zero_wait_compat)
set_tests_properties(execution_zero_wait_compat PROPERTIES DEPENDS execution_zero_wait_compat_compile)
if(MSVC)
    target_compile_options(test_execution_zero_wait_compat PRIVATE /we4996)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_execution_zero_wait_compat PRIVATE -Werror=deprecated-declarations)
endif()
if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_ZERO_WAIT_COMPAT
        test_execution_zero_wait_compat.cpp ON "Suppressed legacy extensions and Forge replacements must compile")
    foreach(_forge_case RANGE 1 2)
        set(CMAKE_REQUIRED_DEFINITIONS ${_forge_extension_defs} -DFORGE_DEPRECATION_CASE=${_forge_case})
        forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_ZERO_WAIT_DEPRECATED_${_forge_case}
            test_execution_zero_wait_compat.cpp OFF "Each legacy extension must diagnose deprecation")
    endforeach()
    set(CMAKE_REQUIRED_DEFINITIONS "${_forge_extension_defs}")
endif()
set(CMAKE_REQUIRED_FLAGS "${_forge_extension_flags}")
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_extension_defs}")
unset(_forge_extension_flags)
unset(_forge_extension_defs)
unset(_forge_case)

# Build separate strict copies of the migrated and zero-value runtime tests.
set(_forge_scope_strict_targets
    test_execution_counting_scope test_execution_counting_scope_limits_runtime
    test_execution_counting_scope_stress test_execution_counting_scope_lifetime
    test_execution_counting_scope_decrement_tail test_execution_spawn_future
    test_execution_static_opstate_scope test_execution_associate_closure
    test_execution_composition test_execution_wave1 test_execution_schedule_from)
set(_forge_scope_strict_copies)
foreach(_forge_target IN LISTS _forge_scope_strict_targets)
    string(REGEX REPLACE "^test_" "" _forge_source_test "${_forge_target}")
    if(_forge_target STREQUAL "test_execution_counting_scope_limits_runtime")
        set(_forge_source_test execution_counting_scope_limits)
    endif()
    forge_add_deprecation_strict_copy(${_forge_target} ${_forge_source_test})
    list(APPEND _forge_scope_strict_copies ${_forge_target}_ds)
endforeach()
add_custom_target(test_execution_extension_deprecations_strict DEPENDS
    ${_forge_scope_strict_copies} test_execution_scope_observer_compat
    test_execution_zero_wait_compat test_execution_extension_compat test_execution_extensions)
unset(_forge_scope_strict_targets)
unset(_forge_scope_strict_copies)
unset(_forge_target)
unset(_forge_source_test)
