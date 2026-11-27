set(FORGE_EXECUTION_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_EXECUTION_RUNTIME_DIR "${FORGE_EXECUTION_TEST_ROOT}/runtime")
set(FORGE_EXECUTION_TEST_SUPPORT_DIR "${FORGE_EXECUTION_TEST_ROOT}/../support/execution")

function(forge_add_execution_runtime_test target_name source_name test_name)
    forge_add_runtime_test(
        ${target_name}
        "${FORGE_EXECUTION_RUNTIME_DIR}/${source_name}"
        ${test_name} ${ARGN})
    target_include_directories(${target_name} PRIVATE "${FORGE_EXECUTION_TEST_SUPPORT_DIR}")
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
endfunction()

function(forge_add_execution_compile_probe target_name source_name test_name)
    forge_add_compile_probe(
        ${target_name}
        "${FORGE_EXECUTION_TEST_ROOT}/compile/${source_name}"
        ${test_name} ${ARGN})
    target_include_directories(${target_name} PRIVATE "${FORGE_EXECUTION_TEST_SUPPORT_DIR}")
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
endfunction()

function(forge_expect_execution_cxx_source_compiles result_var source_name should_compile failure_message)
    set(CMAKE_REQUIRED_INCLUDES
        "${FORGE_EXECUTION_TEST_ROOT}/compile"
        "${FORGE_EXECUTION_TEST_SUPPORT_DIR}")
    forge_expect_cxx_source_compiles(
        ${result_var}
        "${FORGE_EXECUTION_TEST_ROOT}/compile/${source_name}"
        ${should_compile}
        "${failure_message}"
        ${ARGN})
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

# Compile the private L0 shim in its default and forced-portable annotation modes.
forge_add_execution_compile_probe(test_execution_sync_shim
    test_execution_sync_shim.cpp execution_sync_shim_compile OWNER STD)
forge_add_execution_compile_probe(test_execution_sync_shim_no_annotations
    test_execution_sync_shim.cpp execution_sync_shim_no_annotations_compile OWNER STD)
target_compile_definitions(test_execution_sync_shim_no_annotations PRIVATE
    FORGE_EXECUTION_SYNC_SHIM_NO_ANNOTATIONS=1)
if(NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang|Zig)$")
    target_compile_options(test_execution_sync_shim PRIVATE -Werror=thread-safety)
    target_compile_options(test_execution_sync_shim_no_annotations PRIVATE -Werror=thread-safety)
endif()

# The P Mandate is a hard error on use, with a valid instantiation as control.
set(_forge_proxy_probe_defs "${CMAKE_REQUIRED_DEFINITIONS}")
foreach(_case RANGE 1 3)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_proxy_probe_defs}
        -DFORGE_PROXY_QUERY_CASE=${_case} -DFORGE_PROXY_QUERY_VALID=1)
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_PROXY_QUERY_CONTROL_${_case}
        test_execution_proxy_query_mandate.cpp ON "In-place stop token query must compile" OWNER STD)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_proxy_probe_defs} -DFORGE_PROXY_QUERY_CASE=${_case})
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_PROXY_QUERY_REJECT_${_case}
        test_execution_proxy_query_mandate.cpp OFF "try_query must reject an invalid P" OWNER STD)
endforeach()
# Runtime nonrequestability does not excuse an incompatible callback type.
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_proxy_probe_defs} -DFORGE_PROXY_QUERY_VALID=1)
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_PROXY_CALLBACK_CONTROL
    test_execution_proxy_callback_compatibility.cpp ON "A compatible proxy stop callback must compile" OWNER STD)
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_proxy_probe_defs}")
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_PROXY_CALLBACK_REJECT
    test_execution_proxy_callback_compatibility.cpp OFF "A proxy must reject an incompatible stop callback" OWNER STD)
# Isolate the const-lvalue form while both mutable token forms remain available.
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_proxy_probe_defs}
    -DFORGE_PROXY_CALLBACK_CONST_LVALUE=1 -DFORGE_PROXY_QUERY_VALID=1)
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_PROXY_CALLBACK_CONST_LVALUE_CONTROL
    test_execution_proxy_callback_compatibility.cpp ON "A proxy stop callback accepting const Token& must compile" OWNER STD)
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_proxy_probe_defs} -DFORGE_PROXY_CALLBACK_CONST_LVALUE=1)
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_PROXY_CALLBACK_CONST_LVALUE_REJECT
    test_execution_proxy_callback_compatibility.cpp OFF "A proxy must reject a stop callback missing const Token&" OWNER STD)
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_proxy_probe_defs}")
unset(_forge_proxy_probe_defs)
unset(_case)

# The constructor body diagnoses both ways of violating infallibility in env<>.
set(_forge_task_scheduler_defs "${CMAKE_REQUIRED_DEFINITIONS}")
forge_expect_execution_cxx_source_compiles(FORGE_TASK_SCHEDULER_INFALLIBLE_CONTROL
    test_execution_task_scheduler_mandate.cpp ON "An infallible task scheduler must erase" OWNER STD)
foreach(_case IN ITEMS ERROR STOPPED)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_task_scheduler_defs} -DFORGE_TASK_SCHEDULER_${_case}=1)
    forge_expect_execution_cxx_source_compiles(FORGE_TASK_SCHEDULER_REJECT_${_case}
        test_execution_task_scheduler_mandate.cpp OFF "Task scheduler erasure must reject ${_case} in env<>" OWNER STD)
endforeach()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_task_scheduler_defs}")
unset(_forge_task_scheduler_defs)
unset(_case)

# Each task rejection is paired with the accepted sibling coroutine.
set(_forge_task_probe_defs "${CMAKE_REQUIRED_DEFINITIONS}")
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_TASK_CONTROL
    test_execution_task_rejections.cpp ON "The accepted task coroutines must compile" OWNER STD)
foreach(_case IN ITEMS TEMPORARY_REFERENCE NO_ERROR_TARGET AMBIGUOUS_ERROR_TARGET IMMOVABLE_ERROR CV_VOID)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_task_probe_defs} -DFORGE_TASK_REJECT_${_case}=1)
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_TASK_REJECT_${_case}
        test_execution_task_rejections.cpp OFF "task must reject ${_case}" OWNER STD)
endforeach()
# The frame allocator's rebound pointer must be a pointer type.
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_task_probe_defs}")
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_TASK_ALLOCATION_CONTROL
    test_execution_task_allocation_mandate.cpp ON "A task frame allocator with a raw pointer must compile" OWNER STD)
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_task_probe_defs} -DFORGE_TASK_REJECT_FANCY_POINTER=1)
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_TASK_ALLOCATION_REJECT_FANCY_POINTER
    test_execution_task_allocation_mandate.cpp OFF "task frame allocation must reject a fancy pointer" OWNER STD)
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_task_probe_defs}")

# Record how this toolchain implements the optional<U&>::emplace constraint of
# a reference result: 1 library trait, 2 compiler builtin, 0 the stricter
# approximation. The first two are exact.
set(FORGE_EXECUTION_TASK_REFERENCE_BRANCH "")
foreach(_case IN ITEMS 1 2 0)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_task_probe_defs} -DFORGE_TASK_REFERENCE_BRANCH=${_case})
    forge_detect_cxx_source_compiles(FORGE_EXECUTION_TASK_REFERENCE_BRANCH_${_case}
        "${FORGE_EXECUTION_TEST_ROOT}/compile/test_execution_task_reference_branch.cpp" OWNER STD)
    if(FORGE_EXECUTION_TASK_REFERENCE_BRANCH_${_case})
        set(FORGE_EXECUTION_TASK_REFERENCE_BRANCH "${_case}")
        break()
    endif()
endforeach()
if(FORGE_EXECUTION_TASK_REFERENCE_BRANCH STREQUAL "1")
    message(STATUS "Forge: task reference results use std::reference_constructs_from_temporary (exact)")
elseif(FORGE_EXECUTION_TASK_REFERENCE_BRANCH STREQUAL "2")
    message(STATUS "Forge: task reference results use the compiler builtin (exact)")
elseif(FORGE_EXECUTION_TASK_REFERENCE_BRANCH STREQUAL "0")
    # Every supported toolchain has an exact branch. The approximation exists
    # for an unsupported one and must not pass qualification silently.
    message(FATAL_ERROR "task reference results must use an exact constraint on a supported toolchain")
else()
    message(FATAL_ERROR "The task reference-binding branch probe compiled for no branch")
endif()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_task_probe_defs}")
unset(_forge_task_probe_defs)
unset(_case)

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
forge_policy_test_target(test_execution_bulk_policy_constraints DEFAULT)

if(FORGE_TEST_EXPERIMENTAL_POLICIES)
    forge_add_execution_compile_probe(test_execution_bulk_policy_constraints_experimental
        test_execution_bulk_policy_constraints.cpp execution_bulk_policy_constraints_experimental_compile OWNER STD)
    forge_policy_test_target(test_execution_bulk_policy_constraints_experimental EXPERIMENTAL)
endif()

# [exec.adapt.obj] decay-copies the bound arguments, so the pipe form needs a
# copyable policy, which the standard does not promise. Where the native par
# is not copyable (libc++), pin the rejection next to a linked direct call.
function(forge_check_execution_bulk_policy_pipe mode)
    forge_prepare_policy_probe(${mode})
    set(_definitions "${CMAKE_REQUIRED_DEFINITIONS}")
    foreach(_cpo IN ITEMS bulk bulk_chunked bulk_unchunked)
        set(CMAKE_REQUIRED_DEFINITIONS ${_definitions} -DFORGE_TEST_BULK_CPO=${_cpo})
        forge_expect_execution_cxx_source_compiles(FORGE_TEST_${mode}_${_cpo}_DIRECT
            test_execution_bulk_policy_pipe.cpp TRUE
            "The direct ${_cpo} call with a non-copyable par must compile and link (${mode})" OWNER STD)
        list(APPEND CMAKE_REQUIRED_DEFINITIONS -DFORGE_TEST_BULK_REJECT_PIPE=1)
        forge_expect_execution_cxx_source_compiles(FORGE_TEST_${mode}_${_cpo}_REJECT_PIPE
            test_execution_bulk_policy_pipe.cpp FALSE
            "The ${_cpo} closure must reject binding a non-copyable par (${mode})" OWNER STD)
    endforeach()
endfunction()
foreach(_mode IN ITEMS DEFAULT EXPERIMENTAL)
    if(FORGE_TEST_${_mode}_POLICY_OBJECTS AND NOT FORGE_TEST_${_mode}_POLICY_COPYABLE)
        forge_check_execution_bulk_policy_pipe(${_mode})
    endif()
endforeach()
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
_forge_test_owner(_forge_execution_fragment_owner)
target_link_libraries(
    test_execution_fragment_includes PRIVATE
    ${_forge_execution_fragment_owner})
unset(_forge_execution_fragment_owner)
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
forge_add_execution_runtime_test(
    test_execution_test_thread_pool
    test_execution_test_thread_pool.cpp
    execution_test_thread_pool OWNER STD)
forge_add_execution_runtime_test(
    test_execution_adversarial_schedulers
    test_execution_adversarial_schedulers.cpp
    execution_adversarial_schedulers OWNER STD)
forge_add_execution_runtime_test(
    test_execution_parallel_scheduler_replacement
    test_execution_parallel_scheduler_replacement.cpp
    execution_parallel_scheduler_replacement OWNER STD)
forge_add_execution_runtime_test(
    test_execution_task_scheduler
    test_execution_task_scheduler.cpp
    execution_task_scheduler OWNER STD)

forge_add_execution_runtime_test(
    test_execution_task_frame
    test_execution_task_frame.cpp
    execution_task_frame OWNER STD)
if(MSVC)
    target_compile_options(test_execution_task_frame PRIVATE /bigobj)
endif()

forge_add_execution_runtime_test(
    test_execution_task_allocation
    test_execution_task_allocation.cpp
    execution_task_allocation OWNER STD)

function(forge_detect_execution_task_scheduler_policy)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    forge_detect_cxx_source_compiles(FORGE_TEST_TASK_SCHEDULER_NATIVE_POLICY
        "${FORGE_EXECUTION_TEST_ROOT}/compile/test_execution_task_scheduler_policy.cpp" OWNER STD)
    set(FORGE_TEST_TASK_SCHEDULER_NATIVE_POLICY "${FORGE_TEST_TASK_SCHEDULER_NATIVE_POLICY}" PARENT_SCOPE)
endfunction()
forge_detect_execution_task_scheduler_policy()
if(FORGE_TEST_TASK_SCHEDULER_NATIVE_POLICY)
    forge_add_execution_runtime_test(
        test_execution_task_scheduler_bulk
        test_execution_task_scheduler_bulk.cpp
        execution_task_scheduler_bulk OWNER STD)
endif()
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
# The public policy calls live in their own source. It is registered only in
# a mode that has the objects and does not compile in any other. Its CTests
# also fail when no case runs.
set(FORGE_TEST_REQUIRE_GTEST_CASES ON)
if(FORGE_TEST_DEFAULT_POLICY_OBJECTS)
    forge_add_execution_runtime_test(test_execution_bulk_native_policy
        test_execution_bulk_native_policy.cpp execution_bulk_native_policy OWNER STD)
    forge_policy_test_target(test_execution_bulk_native_policy DEFAULT)
endif()
if(FORGE_TEST_EXPERIMENTAL_POLICIES)
    forge_add_execution_runtime_test(test_execution_bulk_native_policy_experimental
        test_execution_bulk_native_policy.cpp execution_bulk_native_policy_experimental OWNER STD)
    forge_policy_test_target(test_execution_bulk_native_policy_experimental EXPERIMENTAL)
endif()
unset(FORGE_TEST_REQUIRE_GTEST_CASES)
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
foreach(_case RANGE 1 33)
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
forge_add_execution_compile_probe(test_execution_scheduling_conformance
    test_execution_scheduling_conformance.cpp execution_scheduling_conformance_compile)
forge_add_execution_runtime_test(test_execution_scheduling_conformance_runtime
    test_execution_scheduling_conformance.cpp execution_scheduling_conformance)
if(MSVC)
    target_compile_options(test_execution_scheduling_conformance PRIVATE /bigobj)
    target_compile_options(test_execution_scheduling_conformance_runtime PRIVATE /bigobj)
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
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_AWAITABLE_LVALUE_BRIDGE_CONTROL
    test_execution_reject_awaitable.cpp ON "An eligible lvalue sender must be accepted by the sender bridge")
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_awaitable_saved_defs}
    -DFORGE_AWAITABLE_CASE=11 -DFORGE_AWAITABLE_POSITIVE=1)
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_AWAITABLE_ADAPTED_VALUE_BRIDGE_CONTROL
    test_execution_reject_awaitable.cpp ON "The sender bridge must accept a single-value sender returned by value")
set(CMAKE_REQUIRED_DEFINITIONS ${_forge_awaitable_saved_defs} -DFORGE_AWAITABLE_CASE=11)
forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_AWAITABLE_ADAPTED_LVALUE_BRIDGE_CONTROL
    test_execution_reject_awaitable.cpp ON "The sender bridge must accept an eligible sender returned by lvalue reference")
foreach(_entry IN ITEMS "12;MOVE_ONLY_LVALUE" "13;CONST_MOVE_ONLY_LVALUE"
                        "14;IMMOVABLE_SOURCE" "15;ADAPTED_MOVE_ONLY_LVALUE")
    list(GET _entry 0 _case)
    list(GET _entry 1 _suffix)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_awaitable_saved_defs}
        -DFORGE_AWAITABLE_CASE=${_case} -DFORGE_AWAITABLE_POSITIVE=1)
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_AWAITABLE_CONTROL_${_suffix}
        test_execution_reject_awaitable.cpp ON "Awaitable bridge positive control ${_suffix} must compile and link")
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_awaitable_saved_defs} -DFORGE_AWAITABLE_CASE=${_case})
    forge_expect_execution_cxx_source_compiles(FORGE_EXECUTION_AWAITABLE_REJECT_${_suffix}
        test_execution_reject_awaitable.cpp OFF "The sender bridge must reject ${_suffix}")
endforeach()
if(MSVC)
    target_compile_options(test_execution_awaitable_conformance PRIVATE /bigobj)
    target_compile_options(test_execution_awaitable_conformance_runtime PRIVATE /bigobj)
endif()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_awaitable_saved_defs}")
unset(_forge_awaitable_saved_defs)
