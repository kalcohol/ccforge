# Forge Tests

# Add googletest
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/googletest ${CMAKE_CURRENT_BINARY_DIR}/googletest)

set(FORGE_TEST_BUILD_CONFIG_ARGS)
if(CMAKE_CONFIGURATION_TYPES)
    list(APPEND FORGE_TEST_BUILD_CONFIG_ARGS --config $<CONFIG>)
endif()

function(forge_expect_cxx_source_compiles result_var source_file should_compile failure_message)
    unset(${result_var} CACHE)
    unset(${result_var})
    file(READ "${source_file}" _forge_probe_source)
    check_cxx_source_compiles("${_forge_probe_source}" ${result_var})

    if(${should_compile})
        if(NOT ${result_var})
            message(FATAL_ERROR "${failure_message}")
        endif()
    else()
        if(${result_var})
            message(FATAL_ERROR "${failure_message}")
        endif()
    endif()
endfunction()

function(forge_add_runtime_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE gtest_main forge::forge)
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

function(forge_add_compile_probe target_name source_file)
    add_executable(${target_name} EXCLUDE_FROM_ALL ${source_file})
    target_link_libraries(${target_name} PRIVATE forge::forge)
    add_test(
        NAME ${target_name}_compile
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target ${target_name})
endfunction()

# Configure-time probes for required std::simd surface. These produce explicit
# configure-time failures when a public entry point regresses instead of letting
# the runtime/compile tests silently compile around the missing API.
include(CheckCXXSourceCompiles)

set(_FORGE_SIMD_PROBE_INCLUDES
    "${CMAKE_SOURCE_DIR}/backport"
    "${CMAKE_SOURCE_DIR}/include")

set(CMAKE_REQUIRED_INCLUDES "${_FORGE_SIMD_PROBE_INCLUDES}")
set(_FORGE_SIMD_CONFIGURE_PROBE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/simd_configure_probes")

forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_WHERE_COMPOUND_OPS
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/where_compound_ops.cpp"
    TRUE
    "Forge simd backport must support where(mask, vec) compound assignment operators")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_WHERE_INT_SCALAR_OPS
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/where_int_scalar_ops.cpp"
    TRUE
    "Forge simd backport must support integral where(mask, vec) scalar compound operators")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_WHERE_BOOL
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/where_bool.cpp"
    TRUE
    "Forge simd backport must support where(bool, vec)")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_WHERE_MASK
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/where_mask.cpp"
    TRUE
    "Forge simd backport must support where(mask, mask)")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_MASK_CAST
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/mask_cast.cpp"
    TRUE
    "Forge simd backport must support mask simd_cast/static_simd_cast")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_ACCEPTS_NON_VALUE_PRESERVING_LOAD_STORE
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/reject_non_value_preserving_load_store.cpp"
    FALSE
    "Forge simd backport must reject non-value-preserving loads/stores without std::simd::flag_convert")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_LONG_DOUBLE_MASK
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/long_double_mask_unary.cpp"
    TRUE
    "Forge simd backport must support long double mask unary operators")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_NATIVE_MASK_UNARY
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/native_mask_unary.cpp"
    TRUE
    "Forge simd backport must support native-width mask unary operators across float, double, and unsigned int")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_IMPLICIT_VEC_CONVERTER
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/reject_implicit_vec_converter.cpp"
    FALSE
    "Forge simd backport must reject vector converting constructors without std::simd::flag_convert")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_FLAG_CONVERT_CONSTRUCTOR
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/flag_convert_constructor.cpp"
    TRUE
    "Forge simd backport must provide vector converting constructors with std::simd::flag_convert")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_VALUE_PRESERVING_VEC_CONVERTER
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/value_preserving_vec_converter.cpp"
    TRUE
    "Forge simd backport must support value-preserving vector converting constructors")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_IMPLICIT_SCALAR_NARROWING_BROADCAST
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/reject_implicit_scalar_narrowing_broadcast.cpp"
    FALSE
    "Forge simd backport must reject non-value-preserving implicit scalar broadcasts")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_NARROWING_GENERATOR
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/reject_narrowing_generator.cpp"
    FALSE
    "Forge simd backport must reject non-value-preserving generator construction")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_RANGE_LOAD_STORE
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/range_load_store.cpp"
    TRUE
    "Forge simd backport must support contiguous range constructors and load/store overloads")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_ACCEPTS_DEQUE_ITERATOR
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/reject_deque_iterator.cpp"
    FALSE
    "Forge simd backport must reject non-contiguous iterators for memory operations")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_RANGE_GATHER_SCATTER
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/range_gather_scatter.cpp"
    TRUE
    "Forge simd backport must support contiguous range gather/scatter overloads")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_COMPRESS_EXPAND_OVERLOADS
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/compress_expand_overloads.cpp"
    TRUE
    "Forge simd backport must provide the repaired compress/expand/select overload set")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_BIT_SURFACE
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/bit_surface.cpp"
    TRUE
    "Forge simd backport must provide the required bit algorithm surface")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_MATH_SURFACE
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/math_surface.cpp"
    TRUE
    "Forge simd backport must provide the required math algorithm surface")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_REJECTS_INTEGRAL_MATH
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/math_rejects_integral.cpp"
    FALSE
    "Forge simd backport must reject floating-only math algorithms on integral vectors")
forge_expect_cxx_source_compiles(
    FORGE_SIMD_HAS_COMPLEX_SURFACE
    "${_FORGE_SIMD_CONFIGURE_PROBE_DIR}/complex_surface.cpp"
    TRUE
    "Forge simd backport must provide the required complex simd surface")

unset(CMAKE_REQUIRED_INCLUDES)

# Runtime tests for unique_resource
add_executable(test_unique_resource test_unique_resource.cpp)
target_link_libraries(test_unique_resource PRIVATE gtest_main)
add_test(NAME unique_resource_runtime COMMAND test_unique_resource)

add_executable(test_unique_resource_checked test_unique_resource_checked.cpp)
target_link_libraries(test_unique_resource_checked PRIVATE gtest_main)
add_test(NAME unique_resource_checked COMMAND test_unique_resource_checked)

add_executable(test_unique_resource_exception test_unique_resource_exception.cpp)
target_link_libraries(test_unique_resource_exception PRIVATE gtest_main)
add_test(NAME unique_resource_exception COMMAND test_unique_resource_exception)

add_executable(test_unique_resource_wording test_unique_resource_wording.cpp)
target_link_libraries(test_unique_resource_wording PRIVATE gtest_main)
add_test(NAME unique_resource_wording COMMAND test_unique_resource_wording)

# Compile probes for target semantics
add_executable(test_unique_resource_api EXCLUDE_FROM_ALL test_unique_resource_api.cpp)
target_link_libraries(test_unique_resource_api PRIVATE forge::forge)
add_test(
    NAME unique_resource_api_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_unique_resource_api)

add_executable(test_unique_resource_checked_noexcept EXCLUDE_FROM_ALL test_unique_resource_checked_noexcept.cpp)
add_test(
    NAME unique_resource_checked_noexcept_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_unique_resource_checked_noexcept)

add_executable(test_unique_resource_include EXCLUDE_FROM_ALL test_unique_resource_include.cpp)
target_link_libraries(test_unique_resource_include PRIVATE forge::forge)
add_test(
    NAME unique_resource_include_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_unique_resource_include)

# Runtime tests for simd
forge_add_runtime_test(test_simd_core test_simd_core.cpp)

forge_add_runtime_test(test_simd_expansion test_simd_expansion.cpp)
target_compile_definitions(test_simd_expansion PRIVATE FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_TESTS)

forge_add_runtime_test(test_simd_reductions test_simd_reductions.cpp)

add_executable(test_simd_abi test_simd_abi.cpp)
target_link_libraries(test_simd_abi PRIVATE gtest_main forge::forge)
add_test(NAME simd_traits COMMAND test_simd_abi)

add_executable(test_simd_mask test_simd_mask.cpp)
target_link_libraries(test_simd_mask PRIVATE gtest_main forge::forge)
add_test(NAME simd_mask COMMAND test_simd_mask)

add_executable(test_simd_memory test_simd_memory.cpp)
target_link_libraries(test_simd_memory PRIVATE gtest_main forge::forge)
target_compile_definitions(test_simd_memory PRIVATE FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS)
add_test(NAME simd_memory COMMAND test_simd_memory)

add_executable(test_simd_operators test_simd_operators.cpp)
target_link_libraries(test_simd_operators PRIVATE gtest_main forge::forge)
add_test(NAME simd_operators COMMAND test_simd_operators)

add_executable(test_simd_math test_simd_math.cpp)
target_link_libraries(test_simd_math PRIVATE gtest_main forge::forge)
add_test(NAME simd_math COMMAND test_simd_math)

add_executable(test_simd_bit test_simd_bit.cpp)
target_link_libraries(test_simd_bit PRIVATE gtest_main forge::forge)
add_test(NAME simd_bit COMMAND test_simd_bit)

add_executable(test_simd_math_ext test_simd_math_ext.cpp)
target_link_libraries(test_simd_math_ext PRIVATE gtest_main forge::forge)
add_test(NAME simd_math_ext COMMAND test_simd_math_ext)

add_executable(test_simd_complex test_simd_complex.cpp)
target_link_libraries(test_simd_complex PRIVATE gtest_main forge::forge)
add_test(NAME simd_complex COMMAND test_simd_complex)

# Compile probes for simd public surface
forge_add_compile_probe(test_simd_api_core test_simd_api_core.cpp)
forge_add_compile_probe(test_simd_api_constructors test_simd_api_constructors.cpp)
forge_add_compile_probe(test_simd_api_memory test_simd_api_memory.cpp)
target_compile_definitions(test_simd_api_memory PRIVATE FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_PROBES)
forge_add_compile_probe(test_simd_api_where test_simd_api_where.cpp)
forge_add_compile_probe(test_simd_api_permute test_simd_api_permute.cpp)
target_compile_definitions(test_simd_api_permute PRIVATE FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_PROBES)
forge_add_compile_probe(test_simd_api_reductions test_simd_api_reductions.cpp)
forge_add_compile_probe(test_simd_api_math test_simd_api_math.cpp)
forge_add_compile_probe(test_simd_api_complex test_simd_api_complex.cpp)

add_executable(test_simd_include EXCLUDE_FROM_ALL test_simd_include.cpp)
target_link_libraries(test_simd_include PRIVATE forge::forge)
add_test(
    NAME simd_include_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_simd_include)

add_executable(test_simd_feature_macro EXCLUDE_FROM_ALL test_simd_feature_macro.cpp)
target_link_libraries(test_simd_feature_macro PRIVATE forge::forge)
add_test(
    NAME simd_feature_macro_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_simd_feature_macro)

add_executable(test_simd_flag_convert_probe EXCLUDE_FROM_ALL test_simd_flag_convert_probe.cpp)
target_link_libraries(test_simd_flag_convert_probe PRIVATE forge::forge)
add_test(
    NAME simd_flag_convert_probe
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_simd_flag_convert_probe)

add_executable(test_simd_native_handoff EXCLUDE_FROM_ALL test_simd_native_handoff.cpp)
target_include_directories(test_simd_native_handoff BEFORE PRIVATE
    "${CMAKE_SOURCE_DIR}/backport"
    "${CMAKE_CURRENT_SOURCE_DIR}/native_handoff_include")
add_test(
    NAME simd_native_handoff_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_simd_native_handoff)

if(TARGET simd_example)
    add_test(NAME simd_example_smoke COMMAND simd_example)
endif()

if(TARGET simd_complete_example)
    add_test(NAME simd_complete_example_smoke COMMAND simd_complete_example)
endif()

# Runtime tests for std::execution (P2300 MVP)
#
# These tests intentionally include <execution> to exercise the wrapper-injection
# shape. They should remain valid even after the standard library ships P2300.
find_library(FORGE_TEST_TBB_LIBRARY NAMES tbb)

add_executable(test_execution_mvp test_execution_mvp.cpp)
target_link_libraries(test_execution_mvp PRIVATE gtest_main forge::forge)
if(FORGE_TEST_TBB_LIBRARY)
    target_link_libraries(test_execution_mvp PRIVATE ${FORGE_TEST_TBB_LIBRARY})
endif()
target_include_directories(test_execution_mvp BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
add_test(NAME execution_mvp COMMAND test_execution_mvp)

add_executable(test_execution_inline_scheduler test_execution_inline_scheduler.cpp)
target_link_libraries(test_execution_inline_scheduler PRIVATE gtest_main forge::forge)
if(FORGE_TEST_TBB_LIBRARY)
    target_link_libraries(test_execution_inline_scheduler PRIVATE ${FORGE_TEST_TBB_LIBRARY})
endif()
target_include_directories(test_execution_inline_scheduler BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
add_test(NAME execution_inline_scheduler COMMAND test_execution_inline_scheduler)

add_executable(test_execution_stop_token test_execution_stop_token.cpp)
target_link_libraries(test_execution_stop_token PRIVATE gtest_main forge::forge)
if(FORGE_TEST_TBB_LIBRARY)
    target_link_libraries(test_execution_stop_token PRIVATE ${FORGE_TEST_TBB_LIBRARY})
endif()
target_include_directories(test_execution_stop_token BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
add_test(NAME execution_stop_token COMMAND test_execution_stop_token)
