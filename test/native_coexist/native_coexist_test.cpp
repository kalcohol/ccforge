// Native coexistence test.
//
// This translation unit pulls in every Forge backport wrapper that injects into
// namespace std (<simd>, <execution>, <mdspan>, <linalg>) through the forge::forge
// include path, and exercises each public API. Its purpose is to catch ODR /
// redefinition regressions in the "seamless injection" handshake:
//
//   * On a toolchain WITHOUT native support, forge.cmake injects the backports
//     and this TU compiles against them.
//   * On a toolchain WITH (even partial) native support, forge.cmake defines the
//     FORGE_HAS_NATIVE_* macros so the wrappers stand aside; this TU must then
//     compile against the NATIVE declarations with no redefinition errors.
//
// A regression that injects a backport on top of partial native declarations
// fails here at compile time.

#include <simd>
#include <execution>
#include <mdspan>
#include <tuple>

#include <gtest/gtest.h>

#if defined(__cpp_lib_mdspan)
#  include <linalg>
#endif

TEST(NativeCoexist, Simd) {
    std::simd::vec<float> v(2.0f);
    const auto sum = std::simd::reduce(v);
    EXPECT_GT(sum, 0.0f);
}

TEST(NativeCoexist, Senders) {
    auto result = std::execution::sync_wait(
        std::execution::just(21)
        | std::execution::then([](int x) { return x * 2; }));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

#if defined(__cpp_lib_mdspan)
TEST(NativeCoexist, Submdspan) {
    int data[12]{};
    std::mdspan<int, std::extents<int, 3, 4>> m(data);
    auto row = std::submdspan(m, 1, std::full_extent);
    EXPECT_EQ(row.extent(0), 4);
}

TEST(NativeCoexist, Linalg) {
    double data[3] = {3.0, 4.0, 0.0};
    std::mdspan<double, std::extents<int, 3>> v(data);
    EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(v), 5.0);
}
#endif
