set(FORGE_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_COMPILE_DIR "${FORGE_TEST_ROOT}/compile")
set(FORGE_RUNTIME_DIR "${FORGE_TEST_ROOT}/runtime")
set(FORGE_EXECUTION_TEST_SUPPORT_DIR "${FORGE_TEST_ROOT}/../support/execution")

# These wrappers explicitly own all registrations using Forge test sources.
function(forge_add_forge_test target_name source_name test_name)
    forge_add_runtime_test(
        ${target_name}
        "${FORGE_RUNTIME_DIR}/${source_name}"
        ${test_name} OWNER FORGE)
    target_include_directories(${target_name} PRIVATE "${FORGE_EXECUTION_TEST_SUPPORT_DIR}")
    target_link_libraries(${target_name} PRIVATE Threads::Threads)
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
endfunction()

function(forge_add_forge_compile_probe target_name source_name test_name)
    forge_add_compile_probe(
        ${target_name}
        "${FORGE_COMPILE_DIR}/${source_name}"
        ${test_name} OWNER FORGE)
    target_include_directories(${target_name} PRIVATE "${FORGE_EXECUTION_TEST_SUPPORT_DIR}")
    target_link_libraries(${target_name} PRIVATE Threads::Threads)
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
endfunction()

function(forge_expect_forge_cxx_source_compiles result_var source_name should_compile failure_message)
    set(CMAKE_REQUIRED_INCLUDES "")
    forge_expect_cxx_source_compiles(
        ${result_var}
        "${FORGE_COMPILE_DIR}/${source_name}"
        ${should_compile}
        "${failure_message}"
        OWNER FORGE)
endfunction()

# <forge/bulk.hpp> provides its three names exactly when senders are usable
# and native policy objects exist. Check the surface in the given policy mode,
# with its real flags, and report the expectation as FORGE_TEST_<mode>_FORGE_BULK.
function(forge_check_bulk_policy_surface mode)
    forge_prepare_policy_probe(${mode})
    set(_source "${FORGE_COMPILE_DIR}/test_forge_bulk_explicit_shapes.cpp")
    set(_expected 0)
    if(FORGE_FORCE_SENDERS_BACKPORT OR FORGE_SENDERS_FULL OR NOT FORGE_SENDERS_PARTIAL)
        if(FORGE_NEEDS_BACKPORT)
            set(_expected "${FORGE_TEST_${mode}_POLICY_OBJECTS}")
        else()
            # Without the wrapper the header falls back to the advertised
            # feature-test macro, not to the presence of the native names.
            set(_definitions "${CMAKE_REQUIRED_DEFINITIONS}")
            list(APPEND CMAKE_REQUIRED_DEFINITIONS -DFORGE_TEST_FORGE_BULK_FALLBACK_ADVERTISED=1)
            forge_detect_cxx_source_compiles(FORGE_TEST_${mode}_FORGE_BULK_FALLBACK "${_source}" OWNER FORGE)
            set(CMAKE_REQUIRED_DEFINITIONS "${_definitions}")
            if(FORGE_TEST_${mode}_FORGE_BULK_FALLBACK)
                set(_expected 1)
            endif()
        endif()
    endif()
    list(APPEND CMAKE_REQUIRED_DEFINITIONS -DFORGE_TEST_EXPECT_FORGE_BULK=${_expected})
    forge_expect_cxx_source_compiles(FORGE_TEST_${mode}_FORGE_BULK_SURFACE
        "${_source}" TRUE
        "The Forge bulk header and its explicit const shapes must compile and link (${mode})" OWNER FORGE)
    if(NOT _expected)
        set(_definitions "${CMAKE_REQUIRED_DEFINITIONS}")
        foreach(_cpo IN ITEMS bulk bulk_chunked bulk_unchunked)
            set(CMAKE_REQUIRED_DEFINITIONS ${_definitions} -DFORGE_TEST_FORGE_BULK_ABSENT=${_cpo})
            forge_expect_cxx_source_compiles(FORGE_TEST_${mode}_FORGE_ABSENT_${_cpo}
                "${_source}" FALSE
                "forge::${_cpo} must be absent when Forge bulk is unavailable (${mode})" OWNER FORGE)
        endforeach()
    endif()
    set(FORGE_TEST_${mode}_FORGE_BULK ${_expected} PARENT_SCOPE)
endfunction()

function(forge_register_simd_extension_tests)
    set(_forge_simd_aliases_available TRUE)
    if(FORGE_FORCE_SIMD_BACKPORT OR (NOT FORGE_SIMD_FULL AND NOT FORGE_SIMD_PARTIAL))
        set(_forge_simd_saved_definitions ${CMAKE_REQUIRED_DEFINITIONS})
        foreach(_forge_legacy_name IN ITEMS FORGE_FIXED_SIZE_ABI FORGE_NATIVE_ABI_TAG)
            set(CMAKE_REQUIRED_DEFINITIONS ${_forge_simd_saved_definitions}
                -DFORGE_SIMD_LEGACY_${_forge_legacy_name}=1)
            forge_expect_forge_cxx_source_compiles(
                FORGE_SIMD_REJECTS_LEGACY_${_forge_legacy_name}
                reject_legacy_forge_simd_names.cpp FALSE
                "Forge SIMD aliases must reject the removed spelling ${_forge_legacy_name}")
            list(APPEND CMAKE_REQUIRED_DEFINITIONS -DFORGE_SIMD_POSITIVE_CONTROL=1)
            forge_expect_forge_cxx_source_compiles(
                FORGE_SIMD_POSITIVE_CONTROL_LEGACY_${_forge_legacy_name}
                reject_legacy_forge_simd_names.cpp TRUE
                "Removed Forge SIMD spelling ${_forge_legacy_name} must have a compiling replacement control")
        endforeach()
    else()
        # Probe the actual alias and trait surface on complete or partial natives.
        set(CMAKE_REQUIRED_INCLUDES "")
        forge_detect_cxx_source_compiles(FORGE_SIMD_FORGE_ALIASES_AVAILABLE
            "${FORGE_COMPILE_DIR}/test_simd_forge_aliases.cpp" OWNER FORGE)
        set(_forge_simd_aliases_available "${FORGE_SIMD_FORGE_ALIASES_AVAILABLE}")
    endif()
    if(_forge_simd_aliases_available)
        forge_add_compile_probe(test_simd_forge_aliases
            "${FORGE_COMPILE_DIR}/test_simd_forge_aliases.cpp" simd_forge_aliases_compile OWNER FORGE)
    endif()
endfunction()

# Keep SIMD-only configurations independent of the general Forge test switch.
if(FORGE_TEST_ENABLE_SIMD)
    forge_register_simd_extension_tests()
endif()
if(NOT FORGE_TEST_ENABLE_ANY_FORGE)
    return()
endif()

find_package(Threads REQUIRED)

if(FORGE_FORCE_SENDERS_BACKPORT
        OR (NOT FORGE_SENDERS_FULL AND NOT FORGE_SENDERS_PARTIAL))
    forge_expect_forge_cxx_source_compiles(
        FORGE_EXECUTION_EXTENSIONS_BACKPORT_CONTROL
        test_forge_execution_extensions_backport.cpp ON
        "Forge execution aliases and split must work with the execution backport")
endif()

forge_check_bulk_policy_surface(DEFAULT)
if(FORGE_TEST_EXPERIMENTAL_POLICIES)
    forge_check_bulk_policy_surface(EXPERIMENTAL)
endif()

if(FORGE_TEST_ENABLE_FORGE_RUNTIME
        OR (FORGE_ENABLE_FORGE_RESOURCE_POLICY
            AND FORGE_TEST_ENABLE_FORGE_RESOURCE)
        OR FORGE_TEST_ENABLE_FORGE_ERASURE)
    add_executable(
        test_forge_execution_header
        "${FORGE_COMPILE_DIR}/test_forge_execution_header.cpp")
    target_link_libraries(test_forge_execution_header PRIVATE
        forge::forge
        Threads::Threads)
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(test_forge_execution_header PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
    add_test(NAME forge_execution_header COMMAND test_forge_execution_header)
    set_tests_properties(forge_execution_header PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endif()

if(FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_forge_compile_probe(
        test_forge_coro_env_header
        test_forge_io_env_header.cpp
        forge_coro_env_header)
    forge_add_forge_compile_probe(
        test_forge_async_stream_header
        test_forge_async_stream_header.cpp
        forge_async_stream_header)
    forge_add_forge_compile_probe(
        test_forge_timer_await_header
        test_forge_timer_await_header.cpp
        forge_timer_await_header)
    forge_add_forge_test(test_forge_byte_vocabulary test_forge_byte_vocabulary.cpp forge_byte_vocabulary)
    forge_add_forge_test(test_forge_memory_streams test_forge_memory_streams.cpp forge_memory_streams)
    forge_add_forge_test(test_forge_stream_concepts test_forge_stream_concepts.cpp forge_stream_concepts)
    forge_add_forge_test(test_forge_async_stream test_forge_async_stream.cpp forge_async_stream)
    forge_add_forge_test(test_forge_coro_io test_forge_coro_io.cpp forge_coro_io)
    # Named forge_coro_* (not forge_io_*): the forge_io test-name prefix is
    # reserved for backend-gated tests, which the FORGE_ENABLE_FORGE_IO=OFF
    # audit asserts register zero tests.
    forge_add_forge_test(
        test_forge_coro_task_alloc
        test_forge_coro_task_alloc.cpp
        forge_coro_task_alloc)
    forge_add_forge_test(test_forge_coro_interop test_forge_coro_interop.cpp forge_coro_interop)
    forge_add_forge_test(test_forge_coro_combinators test_forge_coro_combinators.cpp forge_coro_combinators)
    forge_add_forge_test(test_forge_timer_await test_forge_timer_await.cpp forge_timer_await)
    forge_add_forge_test(test_forge_context_await test_forge_context_await.cpp forge_context_await)
endif()

if(FORGE_TEST_ENABLE_FORGE_RUNTIME)
    forge_add_forge_test(test_forge_any_stop_token test_forge_any_stop_token.cpp forge_any_stop_token)
    forge_add_forge_test(test_forge_thread_pool test_forge_thread_pool.cpp forge_thread_pool)
    target_compile_definitions(
        test_forge_thread_pool
        PRIVATE FORGE_STATIC_THREAD_POOL_ENABLE_TEST_HOOKS)
    forge_add_forge_test(test_forge_start_detached test_forge_start_detached.cpp forge_start_detached)
    forge_add_forge_test(test_forge_single_thread_context test_forge_single_thread_context.cpp forge_single_thread_context)
    forge_add_forge_test(test_forge_timer_context test_forge_timer_context.cpp forge_timer_context)
    forge_add_forge_test(test_forge_runtime_context test_forge_runtime_context.cpp forge_runtime_context)
    forge_add_forge_test(test_forge_task test_forge_task.cpp forge_task)
    forge_add_forge_test(test_forge_async_scope test_forge_async_scope.cpp forge_async_scope)
    forge_add_forge_test(test_forge_channel test_forge_channel.cpp forge_channel)
    forge_add_forge_test(test_forge_resource_context test_forge_resource_context.cpp forge_resource_context)
    forge_add_forge_test(test_forge_strand test_forge_strand.cpp forge_strand)
    forge_add_forge_test(test_forge_strand_rejection test_forge_strand_rejection.cpp forge_strand_rejection)
    forge_add_forge_test(test_forge_runtime_stress test_forge_runtime_stress.cpp forge_runtime_stress)
    forge_add_forge_test(test_forge_scheduler_roundtrip test_forge_scheduler_roundtrip.cpp forge_scheduler_roundtrip)
    forge_add_forge_test(test_forge_wait_result test_forge_wait_result.cpp forge_wait_result)
endif()

if(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND AND FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_forge_test(test_forge_io_context test_forge_io_context.cpp forge_io_context)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-Wl,--wrap=epoll_ctl" FORGE_TEST_HAS_EPOLL_CTL_WRAP)
    if(FORGE_TEST_HAS_EPOLL_CTL_WRAP)
        target_compile_definitions(test_forge_io_context PRIVATE FORGE_TEST_HAS_EPOLL_CTL_WRAP=1)
        target_link_options(test_forge_io_context PRIVATE "-Wl,--wrap=epoll_ctl")
    else()
        message(STATUS
            "Forge: epoll_ctl failure-injection test skipped because the linker lacks --wrap")
    endif()
endif()

if(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND AND FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_forge_test(test_forge_io_iocp test_forge_io_iocp.cpp forge_io_iocp)
endif()

if(FORGE_HAS_FORGE_IO_URING_BACKEND AND FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_forge_compile_probe(
        test_forge_io_uring_context_header
        test_forge_io_uring_context_header.cpp
        forge_io_uring_context_header)
    add_executable(
        test_forge_io_uring_context
        "${FORGE_RUNTIME_DIR}/test_forge_io_uring_context.cpp")
    target_link_libraries(
        test_forge_io_uring_context
        PRIVATE forge::forge Threads::Threads)
    if("${FORGE_ENABLE_FORGE_IO_URING}" STREQUAL "ON")
        target_compile_definitions(
            test_forge_io_uring_context
            PRIVATE FORGE_TEST_IO_URING_RUNTIME_REQUIRED=1)
    endif()
    add_test(
        NAME forge_io_uring_context
        COMMAND test_forge_io_uring_context)
    set_tests_properties(forge_io_uring_context PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
    add_executable(
        test_forge_io_uring_rw
        "${FORGE_RUNTIME_DIR}/test_forge_io_uring_rw.cpp")
    target_link_libraries(
        test_forge_io_uring_rw
        PRIVATE forge::forge Threads::Threads)
    if("${FORGE_ENABLE_FORGE_IO_URING}" STREQUAL "ON")
        target_compile_definitions(
            test_forge_io_uring_rw
            PRIVATE FORGE_TEST_IO_URING_RUNTIME_REQUIRED=1)
    endif()
    add_test(
        NAME forge_io_uring_rw
        COMMAND test_forge_io_uring_rw)
    set_tests_properties(forge_io_uring_rw PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-Wl,--wrap=syscall" FORGE_TEST_HAS_SYSCALL_WRAP)
    if(FORGE_TEST_HAS_SYSCALL_WRAP)
        add_executable(
            test_forge_io_uring_fault
            "${FORGE_RUNTIME_DIR}/test_forge_io_uring_fault.cpp")
        target_link_libraries(
            test_forge_io_uring_fault
            PRIVATE forge::forge Threads::Threads)
        target_link_options(test_forge_io_uring_fault PRIVATE
            "-Wl,--wrap=syscall")
        if("${FORGE_ENABLE_FORGE_IO_URING}" STREQUAL "ON")
            target_compile_definitions(
                test_forge_io_uring_fault
                PRIVATE FORGE_TEST_IO_URING_RUNTIME_REQUIRED=1)
        endif()
        add_test(
            NAME forge_io_uring_fault
            COMMAND test_forge_io_uring_fault)
        set_tests_properties(forge_io_uring_fault PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
    else()
        message(STATUS
            "Forge: io_uring failure-injection test skipped because the linker lacks --wrap")
    endif()
endif()

if(FORGE_ENABLE_FORGE_RESOURCE_POLICY AND FORGE_TEST_ENABLE_FORGE_RESOURCE)
    forge_add_forge_test(test_forge_resource_policy test_forge_resource_policy.cpp forge_resource_policy)
endif()

if(FORGE_TEST_ENABLE_FORGE_ERASURE)
    forge_add_forge_test(test_forge_any_scheduler test_forge_any_scheduler.cpp forge_any_scheduler)
    forge_add_forge_test(test_forge_erased_sender test_forge_erased_sender.cpp forge_erased_sender)
    forge_add_forge_test(test_forge_any_sender test_forge_any_sender.cpp forge_any_sender)
    forge_add_forge_test(test_forge_any_receiver test_forge_any_receiver.cpp forge_any_receiver)
endif()

if(FORGE_TEST_ENABLE_FORGE_RUNTIME)
    forge_add_forge_test(test_forge_task_member_queries
        test_forge_task_member_queries.cpp forge_task_member_queries)
endif()

if(FORGE_TEST_ENABLE_FORGE_RUNTIME OR FORGE_TEST_ENABLE_FORGE_ERASURE
        OR FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_forge_compile_probe(test_forge_member_queries
        test_forge_member_queries.cpp forge_member_queries)
    target_compile_definitions(test_forge_member_queries PRIVATE
        FORGE_TEST_QUERY_RUNTIME=$<BOOL:${FORGE_TEST_ENABLE_FORGE_RUNTIME}>
        FORGE_TEST_QUERY_ERASURE=$<BOOL:${FORGE_TEST_ENABLE_FORGE_ERASURE}>
        FORGE_TEST_QUERY_IO=$<BOOL:${FORGE_TEST_ENABLE_FORGE_IO}>)
endif()

# These cases exercise Forge's contracts with the injected execution implementation.
if(FORGE_FORCE_SENDERS_BACKPORT
        OR (NOT FORGE_SENDERS_FULL AND NOT FORGE_SENDERS_PARTIAL))
    if(TARGET test_forge_member_queries)
        target_sources(test_forge_member_queries PRIVATE
            "${FORGE_COMPILE_DIR}/test_forge_awaitable_conformance.cpp")
        if(MSVC)
            target_compile_options(test_forge_member_queries PRIVATE /bigobj)
        endif()
    endif()

    if(FORGE_TEST_ENABLE_FORGE_ERASURE)
        forge_add_forge_test(test_forge_any_sender_execution_contract
            test_forge_any_sender_execution_contract.cpp forge_any_sender_execution_contract)
    endif()

    if(FORGE_TEST_ENABLE_FORGE_RUNTIME)
        forge_add_forge_test(test_forge_execution_extensions
            test_forge_execution_extensions.cpp forge_execution_extensions)
        # The source does not compile without forge::bulk, so each variant is
        # registered only in a mode that provides it. Its CTests also fail
        # when no case runs.
        set(FORGE_TEST_REQUIRE_GTEST_CASES ON)
        if(FORGE_TEST_DEFAULT_FORGE_BULK)
            forge_add_forge_test(test_forge_serial_bulk
                test_forge_serial_bulk.cpp forge_serial_bulk)
            forge_policy_test_target(test_forge_serial_bulk DEFAULT)
        endif()
        if(FORGE_TEST_EXPERIMENTAL_FORGE_BULK)
            forge_add_forge_test(test_forge_serial_bulk_experimental
                test_forge_serial_bulk.cpp forge_serial_bulk_experimental)
            forge_policy_test_target(test_forge_serial_bulk_experimental EXPERIMENTAL)
        endif()
        unset(FORGE_TEST_REQUIRE_GTEST_CASES)
        forge_add_forge_test(test_forge_predecessor_lifetime
            test_forge_predecessor_lifetime.cpp forge_predecessor_lifetime)
        forge_add_forge_test(test_forge_split_allocations
            test_forge_split_allocations.cpp forge_split_allocations)

        target_sources(test_forge_scheduler_roundtrip PRIVATE
            "${FORGE_RUNTIME_DIR}/test_forge_scheduler_queries.cpp")
        target_sources(test_forge_strand PRIVATE
            "${FORGE_RUNTIME_DIR}/test_forge_scheduling_conformance.cpp")
        target_sources(test_forge_task PRIVATE
            "${FORGE_RUNTIME_DIR}/test_forge_awaitable_interop.cpp"
            "${FORGE_RUNTIME_DIR}/test_forge_task_bridge.cpp")
        target_sources(test_forge_start_detached PRIVATE
            "${FORGE_RUNTIME_DIR}/test_forge_start_detached_integration.cpp")
        target_sources(test_forge_execution_extensions PRIVATE
            "${FORGE_RUNTIME_DIR}/test_forge_split_transfer_integration.cpp"
            "${FORGE_RUNTIME_DIR}/test_forge_split_wave2.cpp")
        target_sources(test_forge_execution_header PRIVATE
            "${FORGE_COMPILE_DIR}/test_forge_scheduling_conformance.cpp")

        if(MSVC)
            foreach(_target IN ITEMS test_forge_predecessor_lifetime test_forge_split_allocations
                    test_forge_scheduler_roundtrip test_forge_strand test_forge_task
                    test_forge_execution_header)
                target_compile_options(${_target} PRIVATE /bigobj)
            endforeach()
        endif()

        # Pair real Forge scheduler controls with the corresponding affine rejections.
        set(_forge_saved_defs "${CMAKE_REQUIRED_DEFINITIONS}")
        foreach(_scheduler RANGE 0 1)
            foreach(_token RANGE 0 1)
                set(CMAKE_REQUIRED_DEFINITIONS ${_forge_saved_defs}
                    -DFORGE_AFFINE_FORGE_SCHEDULER=${_scheduler} -DFORGE_AFFINE_FORGE_TOKEN=${_token}
                    -DFORGE_ADAPTOR_POSITIVE=1)
                forge_expect_forge_cxx_source_compiles(FORGE_AFFINE_FORGE_CONTROL_MEMBER_${_scheduler}_${_token}
                    test_forge_reject_affine.cpp ON "Stoppable Forge affine control must compile and link")
                set(CMAKE_REQUIRED_DEFINITIONS ${_forge_saved_defs}
                    -DFORGE_AFFINE_FORGE_SCHEDULER=${_scheduler} -DFORGE_AFFINE_FORGE_TOKEN=${_token})
                forge_expect_forge_cxx_source_compiles(FORGE_AFFINE_FORGE_REJECT_MEMBER_${_scheduler}_${_token}
                    test_forge_reject_affine.cpp OFF "Unstoppable Forge affine connection must be unavailable")
            endforeach()
        endforeach()

        # The standard-side controls for close/join and continues_on remain in execution.
        foreach(_case RANGE 34 35)
            set(CMAKE_REQUIRED_DEFINITIONS ${_forge_saved_defs}
                -DFORGE_REMOVED_NAME=${_case} -DFORGE_REMOVAL_CONTROL=1)
            forge_expect_forge_cxx_source_compiles(FORGE_EXECUTION_REMOVED_NAME_CONTROL_${_case}
                test_forge_reject_removed_observers.cpp ON "Forge transfer_just control must compile and link")
            set(CMAKE_REQUIRED_DEFINITIONS ${_forge_saved_defs}
                -DFORGE_REMOVED_NAME=${_case} -DFORGE_REMOVAL_CONTROL=0)
            forge_expect_forge_cxx_source_compiles(FORGE_EXECUTION_REMOVED_NAME_REJECT_${_case}
                test_forge_reject_removed_observers.cpp OFF "Removed Forge observer ${_case} must be absent")
        endforeach()
        set(CMAKE_REQUIRED_DEFINITIONS "${_forge_saved_defs}")
        unset(_forge_saved_defs)
    endif()
endif()

if(FORGE_SENDERS_FULL AND NOT FORGE_FORCE_SENDERS_BACKPORT)
    forge_add_forge_compile_probe(
        test_forge_execution_extensions_native
        test_forge_execution_extensions_native.cpp
        forge_execution_extensions_native_compile)
    set_tests_properties(forge_execution_extensions_native_compile PROPERTIES
        FIXTURES_SETUP forge_execution_extensions_native_build)
    add_test(NAME forge_execution_extensions_native
        COMMAND test_forge_execution_extensions_native)
    set_tests_properties(forge_execution_extensions_native PROPERTIES
        FIXTURES_REQUIRED forge_execution_extensions_native_build
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endif()

# Rebuild lifetime-heavy suites with the source/node probes in every TU.
# The test names match both sanitizer lanes' execution|forge selectors.
function(forge_add_stop_lifetime_test stem)
    set(_lifetime_target "test_forge_stop_lifetime_${stem}")
    forge_add_forge_test(${_lifetime_target} "test_forge_${stem}.cpp"
        "forge_stop_callback_strict_lifetime_${stem}")
    target_compile_definitions(${_lifetime_target} PRIVATE
        FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME=1)
    if(MSVC)
        target_compile_options(${_lifetime_target} PRIVATE /bigobj)
    endif()
    if(TARGET test_stop_callback_strict_lifetime)
        add_dependencies(test_stop_callback_strict_lifetime ${_lifetime_target})
    endif()
endfunction()

if(FORGE_TEST_ENABLE_FORGE_RUNTIME)
    foreach(_stem IN ITEMS any_stop_token timer_context channel async_scope wait_result strand)
        forge_add_stop_lifetime_test(${_stem})
    endforeach()
endif()
if(FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_forge_test(test_forge_stop_callback_lifetime
        test_forge_stop_callback_lifetime.cpp forge_stop_callback_lifetime)
    if(MSVC)
        target_compile_options(test_forge_stop_callback_lifetime PRIVATE /bigobj)
    endif()
    foreach(_stem IN ITEMS stop_callback_lifetime coro_interop coro_combinators)
        forge_add_stop_lifetime_test(${_stem})
    endforeach()
endif()
if(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND AND FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_stop_lifetime_test(io_context)
    if(FORGE_TEST_HAS_EPOLL_CTL_WRAP)
        target_compile_definitions(test_forge_stop_lifetime_io_context PRIVATE
            FORGE_TEST_HAS_EPOLL_CTL_WRAP=1)
        target_link_options(test_forge_stop_lifetime_io_context PRIVATE "-Wl,--wrap=epoll_ctl")
    endif()
endif()
if(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND AND FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_stop_lifetime_test(io_iocp)
endif()
if(FORGE_HAS_FORGE_IO_URING_BACKEND AND FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_stop_lifetime_test(io_uring_rw)
    if("${FORGE_ENABLE_FORGE_IO_URING}" STREQUAL "ON")
        target_compile_definitions(test_forge_stop_lifetime_io_uring_rw PRIVATE
            FORGE_TEST_IO_URING_RUNTIME_REQUIRED=1)
    endif()
    set_tests_properties(forge_stop_callback_strict_lifetime_io_uring_rw PROPERTIES
        SKIP_RETURN_CODE 77)
endif()
