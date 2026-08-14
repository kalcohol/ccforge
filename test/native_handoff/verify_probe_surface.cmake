foreach(required_var IN ITEMS
        SOURCE_DIR BINARY_DIR FORGE_ROOT_DIR SURFACE_CASE)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()
if(NOT DEFINED CXX_STANDARD OR CXX_STANDARD STREQUAL "")
    set(CXX_STANDARD 23)
endif()
if(NOT DEFINED GENERATOR OR GENERATOR STREQUAL "")
    set(GENERATOR "Ninja")
endif()

set(configure_args
    "${CMAKE_COMMAND}"
    -G "${GENERATOR}"
    -S "${SOURCE_DIR}"
    -B "${BINARY_DIR}"
    -DCMAKE_CXX_STANDARD=${CXX_STANDARD}
    -DFORGE_ROOT_DIR=${FORGE_ROOT_DIR}
    -DSURFACE_CASE=${SURFACE_CASE})
if(DEFINED CXX_COMPILER AND NOT CXX_COMPILER STREQUAL "")
    list(APPEND configure_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT MAKE_PROGRAM STREQUAL "")
    list(APPEND configure_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()

file(REMOVE_RECURSE "${BINARY_DIR}")
execute_process(
    COMMAND ${configure_args}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)

set(configure_log "${configure_output}\n${configure_error}")
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
        "${SURFACE_CASE} probe-surface configure failed:\n${configure_log}")
endif()

foreach(feature IN ITEMS SIMD MDSPAN_PADDED_LAYOUTS SUBMDSPAN LINALG)
    if(NOT configure_log MATCHES "CC Forge probe: ${feature}=PARTIAL")
        message(FATAL_ERROR
            "${SURFACE_CASE} did not classify ${feature} as PARTIAL:\n${configure_log}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BINARY_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
        "${SURFACE_CASE} probe-surface build failed:\n${build_output}\n${build_error}")
endif()
