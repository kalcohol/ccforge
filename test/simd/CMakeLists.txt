set(FORGE_SIMD_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_SIMD_SUPPORT_DIR "${FORGE_SIMD_TEST_ROOT}/support")
set(FORGE_SIMD_RUNTIME_DIR "${FORGE_SIMD_TEST_ROOT}/runtime")
set(FORGE_SIMD_COMPILE_DIR "${FORGE_SIMD_TEST_ROOT}/compile")
set(FORGE_SIMD_CONFIGURE_PROBE_DIR "${FORGE_SIMD_TEST_ROOT}/configure_probes")
set(FORGE_SIMD_NATIVE_HANDOFF_INCLUDE_DIR "${FORGE_SIMD_COMPILE_DIR}/native_handoff_include")

forge_detect_cxx_source_compiles(
    FORGE_SIMD_HAS_SCALAR_SPECIAL_MATH
    "${FORGE_SIMD_CONFIGURE_PROBE_DIR}/scalar_math_special.cpp")

function(forge_add_simd_runtime_test target_name source_name)
    add_executable(${target_name} "${FORGE_SIMD_RUNTIME_DIR}/${source_name}")
    target_link_libraries(${target_name} PRIVATE gtest_main forge::forge)
    target_include_directories(${target_name} PRIVATE "${FORGE_SIMD_SUPPORT_DIR}")
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

function(forge_add_simd_compile_probe target_name source_name)
    set(test_name "${target_name}_compile")
    if(ARGC GREATER 2)
        set(test_name "${ARGV2}")
    endif()

    add_executable(${target_name} EXCLUDE_FROM_ALL "${FORGE_SIMD_COMPILE_DIR}/${source_name}")
    target_link_libraries(${target_name} PRIVATE forge::forge)
    target_include_directories(${target_name} PRIVATE "${FORGE_SIMD_SUPPORT_DIR}")
    add_test(
        NAME ${test_name}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target ${target_name})
endfunction()

function(forge_add_simd_configure_probe result_var source_name should_compile failure_message)
    forge_expect_cxx_source_compiles(
        ${result_var}
        "${FORGE_SIMD_CONFIGURE_PROBE_DIR}/${source_name}"
        ${should_compile}
        "${failure_message}")
endfunction()

function(forge_register_simd_configure_probes)
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_WHERE_COMPOUND_OPS
        "where_compound_ops.cpp"
        TRUE
        "Forge simd backport must support where(mask, vec) compound assignment operators")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_WHERE_INT_SCALAR_OPS
        "where_int_scalar_ops.cpp"
        TRUE
        "Forge simd backport must support integral where(mask, vec) scalar compound operators")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_WHERE_BOOL
        "where_bool.cpp"
        TRUE
        "Forge simd backport must support where(bool, vec)")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_WHERE_MASK
        "where_mask.cpp"
        TRUE
        "Forge simd backport must support where(mask, mask)")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_MASK_CAST
        "mask_cast.cpp"
        TRUE
        "Forge simd backport must support mask simd_cast/static_simd_cast")
    forge_add_simd_configure_probe(
        FORGE_SIMD_ACCEPTS_NON_VALUE_PRESERVING_LOAD_STORE
        "reject_non_value_preserving_load_store.cpp"
        FALSE
        "Forge simd backport must reject non-value-preserving loads/stores without std::simd::flag_convert")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_LONG_DOUBLE_MASK
        "long_double_mask_unary.cpp"
        TRUE
        "Forge simd backport must support long double mask unary operators")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_NATIVE_MASK_UNARY
        "native_mask_unary.cpp"
        TRUE
        "Forge simd backport must support native-width mask unary operators across float, double, and unsigned int")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_IMPLICIT_VEC_CONVERTER
        "reject_implicit_vec_converter.cpp"
        FALSE
        "Forge simd backport must reject vector converting constructors without std::simd::flag_convert")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_FLAG_CONVERT_CONSTRUCTOR
        "flag_convert_constructor.cpp"
        TRUE
        "Forge simd backport must provide vector converting constructors with std::simd::flag_convert")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_VALUE_PRESERVING_VEC_CONVERTER
        "value_preserving_vec_converter.cpp"
        TRUE
        "Forge simd backport must support value-preserving vector converting constructors")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_IMPLICIT_SCALAR_NARROWING_BROADCAST
        "reject_implicit_scalar_narrowing_broadcast.cpp"
        FALSE
        "Forge simd backport must reject non-value-preserving implicit scalar broadcasts")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_NARROWING_GENERATOR
        "reject_narrowing_generator.cpp"
        FALSE
        "Forge simd backport must reject non-value-preserving generator construction")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_RANGE_LOAD_STORE
        "range_load_store.cpp"
        TRUE
        "Forge simd backport must support contiguous range constructors and load/store overloads")
    forge_add_simd_configure_probe(
        FORGE_SIMD_ACCEPTS_DEQUE_ITERATOR
        "reject_deque_iterator.cpp"
        FALSE
        "Forge simd backport must reject non-contiguous iterators for memory operations")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_RANGE_GATHER_SCATTER
        "range_gather_scatter.cpp"
        TRUE
        "Forge simd backport must support contiguous range gather/scatter overloads")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_COMPRESS_EXPAND_OVERLOADS
        "compress_expand_overloads.cpp"
        TRUE
        "Forge simd backport must provide the repaired compress/expand/select overload set")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_BIT_SURFACE
        "bit_surface.cpp"
        TRUE
        "Forge simd backport must provide the required bit algorithm surface and return types")
    forge_add_simd_configure_probe(
        FORGE_SIMD_REJECTS_SIGNED_BIT
        "bit_rejects_signed.cpp"
        FALSE
        "Forge simd backport must reject unsigned-only bit algorithms on signed vectors")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_MATH_SURFACE
        "math_surface.cpp"
        TRUE
        "Forge simd backport must provide the required math algorithm surface")
    if(FORGE_SIMD_HAS_SCALAR_SPECIAL_MATH)
        forge_add_simd_configure_probe(
            FORGE_SIMD_HAS_MATH_SPECIAL_SURFACE
            "math_special_surface.cpp"
            TRUE
            "Forge simd backport must provide the required special math surface")
    else()
        message(STATUS "Skipping simd special math configure probes: host scalar library lacks C++ special math support")
    endif()
    forge_add_simd_configure_probe(
        FORGE_SIMD_REJECTS_INTEGRAL_MATH
        "math_rejects_integral.cpp"
        FALSE
        "Forge simd backport must reject floating-only math algorithms on integral vectors")
    forge_add_simd_configure_probe(
        FORGE_SIMD_REJECTS_PAIR_DECOMPOSITION
        "math_rejects_pair_decompose.cpp"
        FALSE
        "Forge simd backport must not expose obsolete pair-return decomposition signatures")
    forge_add_simd_configure_probe(
        FORGE_SIMD_REJECTS_CONSTEXPR_INT_ROUND
        "math_rejects_constexpr_int_round.cpp"
        FALSE
        "Forge simd backport must not expose lrint/llrint as constexpr")
    forge_add_simd_configure_probe(
        FORGE_SIMD_HAS_COMPLEX_SURFACE
        "complex_surface.cpp"
        TRUE
        "Forge simd backport must provide the required complex simd surface")
endfunction()

function(forge_register_simd_runtime_tests)
    forge_add_simd_runtime_test(test_simd_core test_simd_core.cpp)

    forge_add_simd_runtime_test(test_simd_expansion test_simd_expansion.cpp)
    target_compile_definitions(test_simd_expansion PRIVATE FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_TESTS)

    forge_add_simd_runtime_test(test_simd_reductions test_simd_reductions.cpp)

    add_executable(test_simd_abi "${FORGE_SIMD_RUNTIME_DIR}/test_simd_abi.cpp")
    target_link_libraries(test_simd_abi PRIVATE gtest_main forge::forge)
    target_include_directories(test_simd_abi PRIVATE "${FORGE_SIMD_SUPPORT_DIR}")
    add_test(NAME simd_traits COMMAND test_simd_abi)

    forge_add_simd_runtime_test(test_simd_mask test_simd_mask.cpp)

    forge_add_simd_runtime_test(test_simd_memory test_simd_memory.cpp)
    target_compile_definitions(test_simd_memory PRIVATE FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS)

    forge_add_simd_runtime_test(test_simd_operators test_simd_operators.cpp)
    forge_add_simd_runtime_test(test_simd_math test_simd_math.cpp)
    forge_add_simd_runtime_test(test_simd_bit test_simd_bit.cpp)
    forge_add_simd_runtime_test(test_simd_math_ext test_simd_math_ext.cpp)
    if(FORGE_SIMD_HAS_SCALAR_SPECIAL_MATH)
        forge_add_simd_runtime_test(test_simd_math_special test_simd_math_special.cpp)
    endif()
    forge_add_simd_runtime_test(test_simd_complex test_simd_complex.cpp)
endfunction()

function(forge_register_simd_compile_probes)
    forge_add_simd_compile_probe(test_simd_api_core test_simd_api_core.cpp)
    forge_add_simd_compile_probe(test_simd_api_constructors test_simd_api_constructors.cpp)
    forge_add_simd_compile_probe(test_simd_api_memory test_simd_api_memory.cpp)
    target_compile_definitions(test_simd_api_memory PRIVATE FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_PROBES)
    forge_add_simd_compile_probe(test_simd_api_where test_simd_api_where.cpp)
    forge_add_simd_compile_probe(test_simd_api_permute test_simd_api_permute.cpp)
    target_compile_definitions(test_simd_api_permute PRIVATE FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_PROBES)
    forge_add_simd_compile_probe(test_simd_api_reductions test_simd_api_reductions.cpp)
    forge_add_simd_compile_probe(test_simd_api_math test_simd_api_math.cpp)
    if(FORGE_SIMD_HAS_SCALAR_SPECIAL_MATH)
        forge_add_simd_compile_probe(test_simd_api_math_special test_simd_api_math_special.cpp)
    endif()
    forge_add_simd_compile_probe(test_simd_api_complex test_simd_api_complex.cpp)
    forge_add_simd_compile_probe(test_simd_include test_simd_include.cpp simd_include_compile)
    forge_add_simd_compile_probe(test_simd_feature_macro test_simd_feature_macro.cpp simd_feature_macro_compile)
    forge_add_simd_compile_probe(test_simd_flag_convert_probe test_simd_flag_convert_probe.cpp simd_flag_convert_probe)

    add_executable(test_simd_native_handoff EXCLUDE_FROM_ALL "${FORGE_SIMD_COMPILE_DIR}/test_simd_native_handoff.cpp")
    target_include_directories(test_simd_native_handoff BEFORE PRIVATE
        "${CMAKE_SOURCE_DIR}/backport"
        "${FORGE_SIMD_NATIVE_HANDOFF_INCLUDE_DIR}"
        "${FORGE_SIMD_SUPPORT_DIR}")
    add_test(
        NAME simd_native_handoff_compile
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_simd_native_handoff)
endfunction()

set(_FORGE_SIMD_SAVED_CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
set(CMAKE_REQUIRED_INCLUDES
    "${CMAKE_SOURCE_DIR}/backport"
    "${CMAKE_SOURCE_DIR}/include")
forge_register_simd_configure_probes()
set(CMAKE_REQUIRED_INCLUDES "${_FORGE_SIMD_SAVED_CMAKE_REQUIRED_INCLUDES}")
unset(_FORGE_SIMD_SAVED_CMAKE_REQUIRED_INCLUDES)

forge_register_simd_runtime_tests()
forge_register_simd_compile_probes()

if(TARGET simd_example)
    add_test(NAME simd_example_smoke COMMAND simd_example)
endif()

if(TARGET simd_complete_example)
    add_test(NAME simd_complete_example_smoke COMMAND simd_complete_example)
endif()
