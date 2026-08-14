# Native handoff: all std-injecting wrappers in one TU. Guards against
# ODR/redefinition regressions when a toolchain ships (partial) native support
# and Forge is expected to stand aside.
forge_add_runtime_test(native_handoff_test native_handoff_test.cpp NativeHandoff)

add_test(
    NAME native_handoff_partial_simd_configure
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/partial_simd_project
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/partial_simd_project
        -DFORGE_ROOT_DIR=${CMAKE_SOURCE_DIR}
        -DCXX_STANDARD=${CMAKE_CXX_STANDARD}
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_partial_simd.cmake)
set_tests_properties(native_handoff_partial_simd_configure PROPERTIES
    TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})

if(NOT MSVC)
    add_test(
        NAME native_handoff_senders_complete_configure
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/senders_complete_project
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/senders_complete_project
            -DFORGE_ROOT_DIR=${CMAKE_SOURCE_DIR}
            -DCXX_STANDARD=${CMAKE_CXX_STANDARD}
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_senders_complete.cmake)
    set_tests_properties(native_handoff_senders_complete_configure PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})

    set(probe_surface_cases
        "sampled|macro"
        "sampled|factory"
        "sampled|set_value"
        "independent_roots|set_error"
        "independent_roots|set_stopped"
        "independent_roots|connect"
        "independent_roots|start"
        "independent_roots|get_env"
        "independent_roots|await_adaptor"
        "independent_roots|sender_marker"
        "independent_roots|scheduler_marker"
        "independent_roots|receiver_marker"
        "independent_roots|operation_state_marker")
    foreach(probe_surface_case IN LISTS probe_surface_cases)
        string(REPLACE "|" ";" probe_surface_parts "${probe_surface_case}")
        list(GET probe_surface_parts 0 surface_case)
        list(GET probe_surface_parts 1 execution_case)
        add_test(
            NAME native_handoff_probe_surface_${surface_case}_${execution_case}
            COMMAND ${CMAKE_COMMAND}
                -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/probe_surface_project
                -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/probe_surface_${surface_case}_${execution_case}
                -DFORGE_ROOT_DIR=${CMAKE_SOURCE_DIR}
                -DSURFACE_CASE=${surface_case}
                -DEXECUTION_CASE=${execution_case}
                -DCXX_STANDARD=${CMAKE_CXX_STANDARD}
                "-DGENERATOR=${CMAKE_GENERATOR}"
                "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
                "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_probe_surface.cmake)
        set_tests_properties(
            native_handoff_probe_surface_${surface_case}_${execution_case}
            PROPERTIES
            TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
    endforeach()
endif()
