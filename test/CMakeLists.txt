# Forge Tests

# Add googletest
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/googletest ${CMAKE_CURRENT_BINARY_DIR}/googletest)

set(FORGE_TEST_BUILD_CONFIG_ARGS)
if(CMAKE_CONFIGURATION_TYPES)
    list(APPEND FORGE_TEST_BUILD_CONFIG_ARGS --config $<CONFIG>)
endif()

# Configure-time probes for optional std::simd surface growth. These let the test
# suite stay green while the implementation lands incrementally, while still
# enabling strict compile/runtime coverage once the API is available.
include(CheckCXXSourceCompiles)

set(_FORGE_SIMD_PROBE_INCLUDES
    "${CMAKE_SOURCE_DIR}/backport"
    "${CMAKE_SOURCE_DIR}/include")

set(CMAKE_REQUIRED_INCLUDES "${_FORGE_SIMD_PROBE_INCLUDES}")

check_cxx_source_compiles([=[
    #include <simd>
    int main() {
        using V = std::simd::vec<int, 4>;
        using M = std::simd::mask<int, 4>;
        V v([](auto i) { return static_cast<int>(decltype(i)::value + 1); });
        V other(2);
        M m(0b0101u);
        std::simd::where(m, v) += other;
        std::simd::where(m, v) *= 3;
        return v[0];
    }
]=] FORGE_SIMD_HAS_WHERE_COMPOUND_OPS)

check_cxx_source_compiles([=[
    #include <simd>
    int main() {
        using V = std::simd::vec<int, 4>;
        using M = std::simd::mask<int, 4>;
        V v([](auto i) { return static_cast<int>(decltype(i)::value + 1); });
        M m(0b0101u);
        std::simd::where(m, v) %= 3;
        std::simd::where(m, v) &= 7;
        std::simd::where(m, v) |= 1;
        std::simd::where(m, v) ^= 2;
        return v[0];
    }
]=] FORGE_SIMD_HAS_WHERE_INT_SCALAR_OPS)

check_cxx_source_compiles([=[
    #include <simd>
    int main() {
        using V = std::simd::vec<int, 4>;
        V v([](auto i) { return static_cast<int>(decltype(i)::value); });
        std::simd::where(true, v) = 1;
        std::simd::where(false, v) = 2;
        return v[0];
    }
]=] FORGE_SIMD_HAS_WHERE_BOOL)

check_cxx_source_compiles([=[
    #include <simd>
    int main() {
        using M = std::simd::mask<int, 4>;
        M cond(0b0101u);
        M target(0u);
        M other(0b1111u);
        std::simd::where(cond, target) = other;
        return target[0] ? 0 : 1;
    }
]=] FORGE_SIMD_HAS_WHERE_MASK)

check_cxx_source_compiles([=[
    #include <simd>
    int main() {
        using M1 = std::simd::mask<int, 4>;
        using M2 = std::simd::mask<long long, 4>;
        M1 value(0b0101u);
        (void)std::simd::simd_cast<M2>(value);
        (void)std::simd::static_simd_cast<M1>(value);
        return 0;
    }
]=] FORGE_SIMD_HAS_MASK_CAST)

check_cxx_source_compiles([=[
    #include <simd>
    int main() {
        using V = std::simd::vec<int, 4>;
        V value(1);
        (void)std::simd::partial_load<V>(static_cast<const float*>(nullptr), std::simd::simd_size_type{4});
        std::simd::partial_store(value, static_cast<float*>(nullptr), std::simd::simd_size_type{4});
        return 0;
    }
]=] FORGE_SIMD_ACCEPTS_NON_VALUE_PRESERVING_LOAD_STORE)

if(FORGE_SIMD_ACCEPTS_NON_VALUE_PRESERVING_LOAD_STORE)
    message(FATAL_ERROR "Forge simd backport must reject non-value-preserving loads/stores without std::simd::flag_convert")
endif()

check_cxx_source_compiles([=[
    #include <simd>
    int main() {
        std::simd::mask<long double, 2> mask(true);
        auto positive = +mask;
        auto negative = -mask;
        auto inverted = ~mask;
        return static_cast<int>(positive[0] + negative[0] + inverted[0]);
    }
]=] FORGE_SIMD_HAS_LONG_DOUBLE_MASK)

check_cxx_source_compiles([=[
    #include <simd>
    int main() {
        std::simd::mask<float, 4> float_mask(0b0101u);
        std::simd::mask<double, 2> double_mask(0b01u);
        std::simd::mask<unsigned int, 4> uint_mask(0b0011u);
        auto positive = +float_mask;
        auto negative = -double_mask;
        auto inverted = ~uint_mask;
        return static_cast<int>(positive[0] + negative[0] + inverted[0]);
    }
]=] FORGE_SIMD_HAS_NATIVE_MASK_UNARY)

check_cxx_source_compiles([=[
    #include <simd>
    int main() {
        std::simd::vec<int, 4> ints(1);
        std::simd::vec<float, 4> converted(ints);
        (void)converted;
        return 0;
    }
]=] FORGE_SIMD_HAS_IMPLICIT_VEC_CONVERTER)

check_cxx_source_compiles([=[
    #include <simd>
    int main() {
        std::simd::vec<int, 4> ints(1);
        std::simd::vec<float, 4> converted(ints, std::simd::flag_convert);
        (void)converted;
        return 0;
    }
]=] FORGE_SIMD_HAS_FLAG_CONVERT_CONSTRUCTOR)

check_cxx_source_compiles([=[
    #include <simd>
    int main() {
        using int4 = std::simd::vec<int, 4>;
        static_assert(alignof(int4) == std::simd::alignment_v<int4>, "alignment_v must match actual alignment");
        return 0;
    }
]=] FORGE_SIMD_HAS_ALIGNMENT_MATCH)

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
add_executable(test_simd test_simd.cpp)
target_link_libraries(test_simd PRIVATE gtest_main forge::forge)
target_compile_definitions(test_simd PRIVATE FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_TESTS)
add_test(NAME simd_runtime COMMAND test_simd)

add_executable(test_simd_abi test_simd_abi.cpp)
target_link_libraries(test_simd_abi PRIVATE gtest_main forge::forge)
add_test(NAME simd_traits COMMAND test_simd_abi)

add_executable(test_simd_mask test_simd_mask.cpp)
target_link_libraries(test_simd_mask PRIVATE gtest_main forge::forge)
if(FORGE_SIMD_HAS_WHERE_MASK)
    target_compile_definitions(test_simd_mask PRIVATE FORGE_SIMD_HAS_WHERE_MASK)
endif()
add_test(NAME simd_mask COMMAND test_simd_mask)

add_executable(test_simd_memory test_simd_memory.cpp)
target_link_libraries(test_simd_memory PRIVATE gtest_main forge::forge)
target_compile_definitions(test_simd_memory PRIVATE FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS)
if(FORGE_SIMD_HAS_WHERE_COMPOUND_OPS)
    target_compile_definitions(test_simd_memory PRIVATE FORGE_SIMD_HAS_WHERE_COMPOUND_OPS)
endif()
if(FORGE_SIMD_HAS_WHERE_INT_SCALAR_OPS)
    target_compile_definitions(test_simd_memory PRIVATE FORGE_SIMD_HAS_WHERE_INT_SCALAR_OPS)
endif()
if(FORGE_SIMD_HAS_WHERE_BOOL)
    target_compile_definitions(test_simd_memory PRIVATE FORGE_SIMD_HAS_WHERE_BOOL)
endif()
add_test(NAME simd_memory COMMAND test_simd_memory)

add_executable(test_simd_operators test_simd_operators.cpp)
target_link_libraries(test_simd_operators PRIVATE gtest_main forge::forge)
if(FORGE_SIMD_HAS_MASK_CAST)
    target_compile_definitions(test_simd_operators PRIVATE FORGE_SIMD_HAS_MASK_CAST)
endif()
add_test(NAME simd_operators COMMAND test_simd_operators)

add_executable(test_simd_math test_simd_math.cpp)
target_link_libraries(test_simd_math PRIVATE gtest_main forge::forge)
add_test(NAME simd_math COMMAND test_simd_math)

# Compile probes for simd public surface
add_executable(test_simd_api EXCLUDE_FROM_ALL test_simd_api.cpp)
target_link_libraries(test_simd_api PRIVATE forge::forge)
target_compile_definitions(test_simd_api PRIVATE FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_PROBES FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_PROBES)
if(FORGE_SIMD_HAS_WHERE_COMPOUND_OPS)
    target_compile_definitions(test_simd_api PRIVATE FORGE_SIMD_HAS_WHERE_COMPOUND_OPS)
endif()
if(FORGE_SIMD_HAS_WHERE_INT_SCALAR_OPS)
    target_compile_definitions(test_simd_api PRIVATE FORGE_SIMD_HAS_WHERE_INT_SCALAR_OPS)
endif()
if(FORGE_SIMD_HAS_WHERE_BOOL)
    target_compile_definitions(test_simd_api PRIVATE FORGE_SIMD_HAS_WHERE_BOOL)
endif()
if(FORGE_SIMD_HAS_WHERE_MASK)
    target_compile_definitions(test_simd_api PRIVATE FORGE_SIMD_HAS_WHERE_MASK)
endif()
if(FORGE_SIMD_HAS_MASK_CAST)
    target_compile_definitions(test_simd_api PRIVATE FORGE_SIMD_HAS_MASK_CAST)
endif()
add_test(
    NAME simd_api_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_simd_api)

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

# Runtime tests for std::execution (P2300 MVP)
#
# These tests intentionally include <execution> to exercise the wrapper-injection
# shape. They should remain valid even after the standard library ships P2300.
find_library(FORGE_TEST_TBB_LIBRARY NAMES tbb)

add_executable(test_execution_mvp test_execution_mvp.cpp)
target_link_libraries(test_execution_mvp PRIVATE gtest_main forge::forge ${FORGE_TEST_TBB_LIBRARY})
target_include_directories(test_execution_mvp BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
add_test(NAME execution_mvp COMMAND test_execution_mvp)

add_executable(test_execution_inline_scheduler test_execution_inline_scheduler.cpp)
target_link_libraries(test_execution_inline_scheduler PRIVATE gtest_main forge::forge ${FORGE_TEST_TBB_LIBRARY})
target_include_directories(test_execution_inline_scheduler BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
add_test(NAME execution_inline_scheduler COMMAND test_execution_inline_scheduler)

add_executable(test_execution_stop_token test_execution_stop_token.cpp)
target_link_libraries(test_execution_stop_token PRIVATE gtest_main forge::forge ${FORGE_TEST_TBB_LIBRARY})
target_include_directories(test_execution_stop_token BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
add_test(NAME execution_stop_token COMMAND test_execution_stop_token)
