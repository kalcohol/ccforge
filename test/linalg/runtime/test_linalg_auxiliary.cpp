#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>
#include <complex>
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

TEST(ScaledAccessorTest, ScalesMatrices) {
    double data[] = {1.0, 2.0, 3.0, 4.0};
    std::mdspan m(data, std::extents<int, 2, 2>{});
    auto sm = std::linalg::scaled(-2.0, m);

    EXPECT_DOUBLE_EQ((sm[0, 0]), -2.0);
    EXPECT_DOUBLE_EQ((sm[0, 1]), -4.0);
    EXPECT_DOUBLE_EQ((sm[1, 0]), -6.0);
    EXPECT_DOUBLE_EQ((sm[1, 1]), -8.0);
}

TEST(TransposedTest, SwapsDimensions) {
    double data[6] = {1,2,3,4,5,6};
    std::mdspan<double, std::extents<int,2,3>> m(data);
    auto mt = std::linalg::transposed(m);
    EXPECT_EQ(mt.extent(0), 3);
    EXPECT_EQ(mt.extent(1), 2);
    EXPECT_EQ(mt.data_handle(), m.data_handle());
    EXPECT_DOUBLE_EQ((mt[0, 1]), 4.0);
    EXPECT_DOUBLE_EQ((mt[2, 1]), 6.0);
    EXPECT_EQ(mt.mapping().stride(0), 1);
    EXPECT_EQ(mt.mapping().stride(1), 3);
}

TEST(ConjugatedTest, RealTypeIsNoop) {
    double data[] = {1.0, 2.0};
    std::mdspan v(data, std::extents<int, 2>{});
    auto cv = std::linalg::conjugated(v);
    EXPECT_DOUBLE_EQ(cv[0], 1.0);
    EXPECT_DOUBLE_EQ(cv[1], 2.0);
}

TEST(ConjugatedTest, ComplexValuesAreConjugated) {
    using complex = std::complex<double>;
    complex data[] = {{1.0, 2.0}, {3.0, -4.0}};
    std::mdspan v(data, std::extents<int, 2>{});
    auto cv = std::linalg::conjugated(v);

    EXPECT_EQ(cv[0], complex(1.0, -2.0));
    EXPECT_EQ(cv[1], complex(3.0, 4.0));
}

TEST(ConjugateTransposedTest, ComposesTransposeAndConjugation) {
    using complex = std::complex<double>;
    complex data[] = {{1.0, 1.0}, {2.0, -3.0}, {4.0, 5.0}, {6.0, -7.0}};
    std::mdspan m(data, std::extents<int, 2, 2>{});
    auto ct = std::linalg::conjugate_transposed(m);

    EXPECT_EQ(ct.extent(0), 2);
    EXPECT_EQ(ct.extent(1), 2);
    EXPECT_EQ((ct[0, 1]), (complex(4.0, -5.0)));
    EXPECT_EQ((ct[1, 0]), (complex(2.0, 3.0)));
}

TEST(LayoutBlasPackedTest, UpperAndLowerColumnMajorMappings) {
    using extents_t = std::extents<int, 3, 3>;
    using upper_layout = std::linalg::layout_blas_packed<
        std::linalg::upper_triangle_t, std::linalg::column_major_t>;
    using lower_layout = std::linalg::layout_blas_packed<
        std::linalg::lower_triangle_t, std::linalg::column_major_t>;

    upper_layout::mapping<extents_t> upper(extents_t{});
    lower_layout::mapping<extents_t> lower(extents_t{});

    EXPECT_EQ(upper.required_span_size(), 6);
    EXPECT_FALSE(upper.is_unique());
    EXPECT_EQ(upper(0, 0), 0);
    EXPECT_EQ(upper(0, 1), 1);
    EXPECT_EQ(upper(1, 1), 2);
    EXPECT_EQ(upper(2, 0), 3);
    EXPECT_EQ(upper(2, 2), 5);

    EXPECT_EQ(lower.required_span_size(), 6);
    EXPECT_FALSE(lower.is_unique());
    EXPECT_EQ(lower(0, 0), 0);
    EXPECT_EQ(lower(1, 0), 1);
    EXPECT_EQ(lower(2, 0), 2);
    EXPECT_EQ(lower(1, 1), 3);
    EXPECT_EQ(lower(0, 2), 2);
}

TEST(LayoutBlasPackedTest, SingleElementMappingsAreUnique) {
    using extents_t = std::extents<int, 1, 1>;
    using upper_layout = std::linalg::layout_blas_packed<
        std::linalg::upper_triangle_t, std::linalg::column_major_t>;

    upper_layout::mapping<extents_t> mapping(extents_t{});
    static_assert(upper_layout::mapping<extents_t>::is_always_unique());
    EXPECT_TRUE(mapping.is_unique());
    EXPECT_EQ(mapping.required_span_size(), 1);
}

TEST(LayoutBlasPackedTest, UpperAndLowerRowMajorMappings) {
    using extents_t = std::extents<int, 3, 3>;
    using upper_layout = std::linalg::layout_blas_packed<
        std::linalg::upper_triangle_t, std::linalg::row_major_t>;
    using lower_layout = std::linalg::layout_blas_packed<
        std::linalg::lower_triangle_t, std::linalg::row_major_t>;

    upper_layout::mapping<extents_t> upper(extents_t{});
    lower_layout::mapping<extents_t> lower(extents_t{});

    EXPECT_EQ(upper.required_span_size(), 6);
    EXPECT_EQ(upper(0, 0), 0);
    EXPECT_EQ(upper(0, 2), 2);
    EXPECT_EQ(upper(1, 1), 3);
    EXPECT_EQ(upper(2, 1), 4);
    EXPECT_EQ(upper(2, 2), 5);

    EXPECT_EQ(lower.required_span_size(), 6);
    EXPECT_EQ(lower(0, 0), 0);
    EXPECT_EQ(lower(1, 0), 1);
    EXPECT_EQ(lower(1, 1), 2);
    EXPECT_EQ(lower(0, 2), 3);
    EXPECT_EQ(lower(2, 2), 5);
}
