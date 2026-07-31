// Native handoff test.
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
#include <concepts>
#include <tuple>
#include <type_traits>

#include <gtest/gtest.h>

#if defined(__cpp_lib_mdspan)
#  include <linalg>
#endif

template<class Left, class Right>
concept native_handoff_mapping_equality = requires(
    const Left& left, const Right& right) {
    { left == right } -> std::convertible_to<bool>;
};

TEST(NativeHandoff, Simd) {
    std::simd::vec<float> v(2.0f);
    const auto sum = std::simd::reduce(v);
    EXPECT_GT(sum, 0.0f);
}

TEST(NativeHandoff, Senders) {
    auto result = std::execution::sync_wait(
        std::execution::just(21)
        | std::execution::then([](int x) { return x * 2; }));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

#if defined(__cpp_lib_mdspan)
TEST(NativeHandoff, Submdspan) {
    int data[12]{};
    std::mdspan<int, std::extents<int, 3, 4>> m(data);
    auto row = std::submdspan(m, 1, std::full_extent);
    auto sub_ext = std::subextents(m.extents(), std::full_extent, std::range_slice{0, 4, 2});
    auto canonical = std::canonical_slices(m.extents(), std::full_extent, std::range_slice{0, 4, 2});
    using extent_slice_t = std::extent_slice<int, int, int>;
    using range_slice_t = std::range_slice<int, int>;
    using left_padded_t = std::layout_left_padded<8>::mapping<std::extents<int, 3, 4>>;
    using right_padded_t = std::layout_right_padded<8>::mapping<std::extents<int, 3, 4>>;
    using left_compact_t = std::layout_left::mapping<std::extents<int, 3, 4>>;
    using right_compact_t = std::layout_right::mapping<std::extents<int, 3, 4>>;
    using strided_t = std::layout_stride::mapping<std::extents<int, 3, 4>>;
    static_assert(std::is_convertible_v<left_padded_t, left_compact_t>);
    static_assert(std::is_convertible_v<right_padded_t, right_compact_t>);
    static_assert(std::is_convertible_v<left_padded_t, strided_t>);
    static_assert(std::is_convertible_v<right_padded_t, strided_t>);
    static_assert(!native_handoff_mapping_equality<
                  left_padded_t, right_padded_t>);
    EXPECT_EQ(row.extent(0), 4);
    EXPECT_EQ(sub_ext.extent(1), 2);
    EXPECT_EQ(std::tuple_size_v<decltype(canonical)>, 2u);
    EXPECT_EQ((left_padded_t{std::extents<int, 3, 4>{}, 8}.stride(1)), 8);
    EXPECT_EQ((right_padded_t{std::extents<int, 3, 4>{}, 8}.stride(0)), 8);
    (void)sizeof(extent_slice_t);
    (void)sizeof(range_slice_t);
}

TEST(NativeHandoff, Linalg) {
    double data[3] = {3.0, 4.0, 0.0};
    std::mdspan<double, std::extents<int, 3>> v(data);
    EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(v), 5.0);
}
#endif
