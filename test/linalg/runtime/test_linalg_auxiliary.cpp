#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>
#include <type_traits>

TEST(LinalgTagsTest, TagsAreEmptyTypes) {
    static_assert(std::is_empty_v<std::linalg::column_major_t>);
    static_assert(std::is_empty_v<std::linalg::row_major_t>);
    static_assert(std::is_empty_v<std::linalg::upper_triangle_t>);
    static_assert(std::is_empty_v<std::linalg::lower_triangle_t>);
    static_assert(std::is_empty_v<std::linalg::implicit_unit_diagonal_t>);
    static_assert(std::is_empty_v<std::linalg::explicit_diagonal_t>);
    SUCCEED();
}

TEST(ScaledAccessorTest, ScalesValues) {
    double data[] = {1.0, 2.0, 3.0};
    std::mdspan v(data, std::extents<int, 3>{});
    auto sv = std::linalg::scaled(2.0, v);
    EXPECT_DOUBLE_EQ(sv[0], 2.0);
    EXPECT_DOUBLE_EQ(sv[1], 4.0);
    EXPECT_DOUBLE_EQ(sv[2], 6.0);
}

TEST(TransposedTest, SwapsDimensions) {
    double data[6] = {1,2,3,4,5,6};
    std::mdspan<double, std::extents<int,2,3>> m(data);
    auto mt = std::linalg::transposed(m);
    EXPECT_EQ(mt.extent(0), 3);
    EXPECT_EQ(mt.extent(1), 2);
}

TEST(ConjugatedTest, RealTypeIsNoop) {
    double data[] = {1.0, 2.0};
    std::mdspan v(data, std::extents<int, 2>{});
    auto cv = std::linalg::conjugated(v);
    EXPECT_DOUBLE_EQ(cv[0], 1.0);
    EXPECT_DOUBLE_EQ(cv[1], 2.0);
}
