cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/HiveTestExpectations.cmake")

foreach(required SOURCE_DIR BINARY_DIR FORGE_ROOT_DIR HIVE_CASE_NAME PARENT_CACHE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
if(NOT CXX_STANDARD)
    set(CXX_STANDARD 23)
endif()
if(NOT GENERATOR)
    set(GENERATOR Ninja)
endif()

set(case_names none macro_old macro_current limits template pmr complete broken_body)
list(FIND case_names "${HIVE_CASE_NAME}" hive_case)
set(force OFF)
if(HIVE_CASE_NAME STREQUAL "force")
    set(hive_case 0)
    set(force ON)
elseif(HIVE_CASE_NAME STREQUAL "warm")
    set(hive_case 6)
elseif(hive_case LESS 0)
    message(FATAL_ERROR "unknown Hive fixture")
endif()
set(roots "")
set(state PARTIAL)
if(hive_case EQUAL 0)
    set(state BACKPORT)
elseif(hive_case EQUAL 1 OR hive_case EQUAL 2)
    set(roots MACRO)
elseif(hive_case EQUAL 3)
    set(roots LIMITS)
elseif(hive_case EQUAL 4)
    set(roots TEMPLATE)
elseif(hive_case EQUAL 5)
    set(roots PMR)
else()
    set(roots MACRO,LIMITS,TEMPLATE,PMR)
    if(hive_case EQUAL 6)
        set(state COMPLETE)
    endif()
endif()
if(force)
    set(state FORCED)
endif()

file(REMOVE_RECURSE "${BINARY_DIR}")
set(probe_dir "${FORGE_ROOT_DIR}/cmake")
if(HIVE_CASE_NAME STREQUAL "warm")
    # Edit only a private module copy. The source worktree is never modified.
    file(COPY "${FORGE_ROOT_DIR}/cmake/" DESTINATION "${BINARY_DIR}/probe-cmake")
    set(probe_dir "${BINARY_DIR}/probe-cmake")
endif()

function(hive_configure_and_build phase expected_state expected_roots)
    set(args "${CMAKE_COMMAND}" -G "${GENERATOR}"
        -C "${PARENT_CACHE}"
        -S "${SOURCE_DIR}" -B "${BINARY_DIR}"
        "-DCMAKE_CXX_STANDARD=${CXX_STANDARD}"
        "-DFORGE_ROOT_DIR=${FORGE_ROOT_DIR}"
        "-DFORGE_CMAKE_DIR=${probe_dir}"
        "-DHIVE_EXPECT_PROBE_DIR=${probe_dir}"
        "-DHIVE_CASE=${hive_case}"
        "-DFORGE_FORCE_HIVE_BACKPORT=${force}"
        "-DEXPECT_STATE=${expected_state}"
        "-DEXPECT_ROOTS=${expected_roots}")
    if(CXX_COMPILER)
        list(APPEND args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
    endif()
    if(MAKE_PROGRAM)
        list(APPEND args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
    endif()
    if(GENERATOR_PLATFORM)
        list(APPEND args -A "${GENERATOR_PLATFORM}")
    endif()
    if(GENERATOR_TOOLSET)
        list(APPEND args -T "${GENERATOR_TOOLSET}")
    endif()
    execute_process(COMMAND ${args} RESULT_VARIABLE result
        OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${BINARY_DIR}/${phase}-configure.log" "${output}\n${error}")
    if(NOT result EQUAL 0 OR
            NOT "${output}" MATCHES "CC Forge probe: HIVE=${expected_state}")
        message(FATAL_ERROR "Hive ${phase} configure failed; see ${BINARY_DIR}/${phase}-configure.log")
    endif()
    set(config Release)
    if(TEST_CONFIG)
        set(config "${TEST_CONFIG}")
    endif()
    # Query generated CTest registration without executing any registered test.
    execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${BINARY_DIR}"
        -C "${config}" --show-only=json-v1
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${BINARY_DIR}/${phase}-registration.log" "${output}\n${error}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Hive ${phase} CTest listing failed; see ${BINARY_DIR}/${phase}-registration.log")
    endif()
    string(JSON test_count LENGTH "${output}" tests)
    set(hive_tests "")
    if(test_count GREATER 0)
        math(EXPR last_test "${test_count} - 1")
        foreach(index RANGE 0 ${last_test})
            string(JSON name GET "${output}" tests ${index} name)
            if(name MATCHES "^hive_")
                list(APPEND hive_tests "${name}")
            endif()
        endforeach()
    endif()
    hive_expected_registrations("${expected_state}" expected_targets expected_tests)
    hive_assert_registrations("Hive ${phase} CTest listing (${expected_state})"
        "${hive_tests}" "${expected_tests}")
    # These headers model probe roots, including declarations-only and broken
    # bodies. Build only the wrapper consumer, never the conformance targets.
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BINARY_DIR}" --config "${config}"
        --target hive_consumer
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${BINARY_DIR}/${phase}-build.log" "${output}\n${error}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Hive ${phase} shared-root consumer failed; see ${BINARY_DIR}/${phase}-build.log")
    endif()
endfunction()

hive_configure_and_build(initial "${state}" "${roots}")
if(HIVE_CASE_NAME STREQUAL "warm")
    set(module "${probe_dir}/probes/ForgeProbeHive.cmake")
    file(READ "${module}" original)
    # Previously positive FULL/MACRO/LIMITS/TEMPLATE decisions must be cleared
    # solely because the modular source changed. Leave PMR as the sole root.
    string(REPLACE "using H =" "#error warm FULL control\n    using H =" edited "${original}")
    string(REPLACE "#ifndef __cpp_lib_hive" "#if 1" edited "${edited}")
    string(REPLACE "using probe = std::hive_limits;"
        "using probe = std::missing_hive_limits;" edited "${edited}")
    string(REPLACE "accepts_hive<std::hive>"
        "accepts_hive<std::missing_hive>" edited "${edited}")
    file(WRITE "${module}" "${edited}")
    hive_configure_and_build(changed PARTIAL PMR)
    file(WRITE "${module}" "${original}")
    hive_configure_and_build(restored COMPLETE "${roots}")
endif()
