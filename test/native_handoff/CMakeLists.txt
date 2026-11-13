# Native handoff: all std-injecting wrappers in one TU. Guards against
# ODR/redefinition regressions when a toolchain ships (partial) native support
# and Forge is expected to stand aside.
forge_add_runtime_test(native_handoff_test native_handoff_test.cpp NativeHandoff)

# Synthetic policy/mechanics controls, not claims about native Hive quality.
# Preserve the parent's standard library, sanitizer and platform selection in
# nested configures. An initial cache avoids shell/list splitting of flags.
set(_hive_parent_cache "")
set(_hive_parent_variables
    CMAKE_BUILD_TYPE CMAKE_CXX_COMPILER_TARGET CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN
    CMAKE_CXX_COMPILER_LAUNCHER CMAKE_CXX_LINKER_LAUNCHER
    CMAKE_SYSROOT CMAKE_SYSROOT_COMPILE CMAKE_SYSROOT_LINK CMAKE_TOOLCHAIN_FILE
    CMAKE_OSX_SYSROOT CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET
    CMAKE_MSVC_RUNTIME_LIBRARY CMAKE_CXX_STANDARD_LIBRARIES)
foreach(_hive_flag_base CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS)
    list(APPEND _hive_parent_variables ${_hive_flag_base})
    foreach(_hive_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER "${_hive_config}" _hive_config)
        list(APPEND _hive_parent_variables ${_hive_flag_base}_${_hive_config})
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _hive_parent_variables)
foreach(_hive_variable IN LISTS _hive_parent_variables)
    if(DEFINED ${_hive_variable})
        string(APPEND _hive_parent_cache
            "set(${_hive_variable} [==[${${_hive_variable}}]==] CACHE STRING \"Hive parent setting\" FORCE)\n")
    endif()
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hive_parent_$<CONFIG>.cmake"
    CONTENT "${_hive_parent_cache}")
foreach(hive_case none macro_old macro_current limits template pmr complete broken_body force warm)
    add_test(NAME native_handoff_hive_${hive_case}
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/hive_project
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/hive_${hive_case}
            -DFORGE_ROOT_DIR=${CMAKE_SOURCE_DIR}
            -DHIVE_CASE_NAME=${hive_case}
            -DCXX_STANDARD=${CMAKE_CXX_STANDARD}
            "-DPARENT_CACHE=${CMAKE_CURRENT_BINARY_DIR}/hive_parent_$<CONFIG>.cmake"
            "-DTEST_CONFIG=$<CONFIG>"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/hive_verify.cmake)
    set_tests_properties(native_handoff_hive_${hive_case} PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endforeach()

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
        "independent_roots|as_awaitable"
        "independent_roots|schedule_from"
        "independent_roots|transform_sender"
        "independent_roots|apply_sender"
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

# Configuration contracts run with the native-handoff group in existing lanes.
function(_forge_add_configuration_test name case mode standard)
    add_test(NAME native_handoff_configuration_${name}
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/configuration_project
            "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/configuration_${name} space"
            -DFORGE_SOURCE=${CMAKE_SOURCE_DIR}
            -DCASE=${case} -DMODE=${mode} -DPROBE_STANDARD=${standard}
            "-DPARENT_CACHE=${CMAKE_CURRENT_BINARY_DIR}/hive_parent_$<CONFIG>.cmake"
            "-DTEST_CONFIG=$<CONFIG>"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_configuration.cmake)
    set_tests_properties(native_handoff_configuration_${name} PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endfunction()
foreach(_case consistent private same same_empty same_undefined
        different different_standard different_extensions unset
        different_deployment different_platform different_environment
        c_language same_header same_toolchain different_header different_toolchain)
    _forge_add_configuration_test(${_case} ${_case} include 23)
endforeach()
foreach(_mode subdirectory fetchcontent package)
    _forge_add_configuration_test(${_mode} same ${_mode} 23)
    if(NOT _mode STREQUAL "package")
        _forge_add_configuration_test(${_mode}_default source_default ${_mode} 23)
    endif()
endforeach()
# CMake must know how to request C++26 for these consumers to reach compilation.
if("cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    foreach(_case mismatch features transitive)
        _forge_add_configuration_test(${_case} ${_case} include 23)
    endforeach()
    _forge_add_configuration_test(promotion promotion include 26)
    foreach(_mode subdirectory fetchcontent package)
        _forge_add_configuration_test(${_mode}_mismatch mismatch ${_mode} 23)
    endforeach()
endif()
