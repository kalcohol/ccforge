#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>
#include <array>
#include <complex>
#include <type_traits>

namespace {

struct unconjugated_value {
    double value{};
};

namespace custom_conjugation {
struct value {
    double real{};
    double imaginary{};
};

constexpr value conj(const value& x) {
    return {x.real, -x.imaginary};
}
} // namespace custom_conjugation

constexpr bool transformed_views_are_constexpr() {
    double vector_data[] = {1.0, 2.0, 3.0};
    std::mdspan vector(vector_data, std::extents<int, 3>{});
    const auto scaled = std::linalg::scaled(2.0, vector);

    double matrix_data[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::mdspan matrix(matrix_data, std::extents<int, 2, 3>{});
    const auto transposed = std::linalg::transposed(matrix);

    std::complex<double> complex_data[] = {{1.0, 2.0}};
    std::mdspan complex_vector(
        complex_data, std::extents<int, 1>{});
    const auto conjugated = std::linalg::conjugated(complex_vector);
    const auto conjugate_transposed =
        std::linalg::conjugate_transposed(
            std::mdspan(
                complex_data, std::extents<int, 1, 1>{}));

    return scaled[2] == 6.0 && transposed[2, 1] == 6.0 &&
           conjugated[0] == std::complex<double>(1.0, -2.0) &&
           conjugate_transposed[0, 0] ==
               std::complex<double>(1.0, -2.0);
}

static_assert(transformed_views_are_constexpr());

using scaled_accessor_t = std::linalg::scaled_accessor<
    double, std::default_accessor<double>>;
using conjugated_accessor_t = std::linalg::conjugated_accessor<
    std::default_accessor<std::complex<double>>>;
static_assert(std::is_same_v<scaled_accessor_t::reference, double>);
static_assert(std::is_same_v<
              conjugated_accessor_t::reference, std::complex<double>>);
static_assert(std::is_convertible_v<
              std::default_accessor<std::complex<double>>,
              conjugated_accessor_t>);

using packed_extents_t = std::extents<int, 3, 3>;
using packed_mapping_t = std::linalg::layout_blas_packed<
    std::linalg::upper_triangle_t,
    std::linalg::column_major_t>::mapping<packed_extents_t>;
static_assert(std::is_convertible_v<packed_extents_t, packed_mapping_t>);

} // namespace

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
    static_assert(std::is_same_v<
        typename decltype(mt)::layout_type, std::layout_left>);
    EXPECT_EQ(mt.extent(0), 3);
    EXPECT_EQ(mt.extent(1), 2);
    EXPECT_EQ(mt.data_handle(), m.data_handle());
    EXPECT_DOUBLE_EQ((mt[0, 1]), 4.0);
    EXPECT_DOUBLE_EQ((mt[2, 1]), 6.0);
    EXPECT_EQ(mt.mapping().stride(0), 1);
    EXPECT_EQ(mt.mapping().stride(1), 3);
}

TEST(TransposedTest, SelectsTheStandardLayoutMapping) {
    double data[32]{};
    using extents_t = std::extents<int, 2, 3>;

    std::mdspan<double, extents_t, std::layout_left> left(data);
    auto left_t = std::linalg::transposed(left);
    static_assert(std::is_same_v<
        typename decltype(left_t)::layout_type, std::layout_right>);
    EXPECT_EQ(left_t.mapping().stride(0), 2);
    EXPECT_EQ(left_t.mapping().stride(1), 1);

    const std::array<int, 2> strides{5, 1};
    std::layout_stride::mapping<extents_t> stride_mapping(extents_t{}, strides);
    std::mdspan<double, extents_t, std::layout_stride> strided(
        data, stride_mapping);
    auto strided_t = std::linalg::transposed(strided);
    static_assert(std::is_same_v<
        typename decltype(strided_t)::layout_type, std::layout_stride>);
    EXPECT_EQ(strided_t.mapping().stride(0), 1);
    EXPECT_EQ(strided_t.mapping().stride(1), 5);

    using padded_layout = std::layout_left_padded<8>;
    using padded_mapping = padded_layout::mapping<extents_t>;
    std::mdspan<double, extents_t, padded_layout> padded(
        data, padded_mapping(extents_t{}, 8));
    auto padded_t = std::linalg::transposed(padded);
    static_assert(std::is_same_v<
        typename decltype(padded_t)::layout_type,
        std::layout_right_padded<8>>);
    EXPECT_EQ(padded_t.mapping().stride(0), 8);
    EXPECT_EQ(padded_t.mapping().stride(1), 1);
}

TEST(TransposedTest, ReversesPackedAndNestedTransposeLayouts) {
    double data[16] = {
        0, 1, 2, 3, 4, 5, 6, 7,
        8, 9, 10, 11, 12, 13, 14, 15};
    using extents_t = std::extents<int, 3, 3>;
    using packed_layout = std::linalg::layout_blas_packed<
        std::linalg::upper_triangle_t, std::linalg::column_major_t>;
    std::mdspan<double, extents_t, packed_layout> packed(data);

    auto packed_t = std::linalg::transposed(packed);
    using expected_layout = std::linalg::layout_blas_packed<
        std::linalg::lower_triangle_t, std::linalg::row_major_t>;
    static_assert(std::is_same_v<
        typename decltype(packed_t)::layout_type, expected_layout>);
    EXPECT_EQ((packed_t[2, 0]), (packed[0, 2]));

    auto twice = std::linalg::transposed(
        std::linalg::transposed(
            std::mdspan<double, std::extents<int, 2, 3>>(data)));
    static_assert(std::is_same_v<
        typename decltype(twice)::layout_type, std::layout_right>);
    EXPECT_EQ(twice.extent(0), 2);
    EXPECT_EQ(twice.extent(1), 3);
}

TEST(ConjugatedTest, RealTypeIsNoop) {
    double data[] = {1.0, 2.0};
    std::mdspan v(data, std::extents<int, 2>{});
    auto cv = std::linalg::conjugated(v);
    static_assert(std::is_same_v<decltype(cv), decltype(v)>);
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

TEST(ConjugatedTest, DoubleConjugationRestoresTheNestedAccessor) {
    using complex = std::complex<double>;
    complex data[] = {{1.0, 2.0}, {3.0, -4.0}};
    std::mdspan v(data, std::extents<int, 2>{});

    auto restored = std::linalg::conjugated(std::linalg::conjugated(v));
    static_assert(std::is_same_v<decltype(restored), decltype(v)>);
    EXPECT_EQ(restored.data_handle(), v.data_handle());
    EXPECT_EQ(restored[0], data[0]);
    EXPECT_EQ(restored[1], data[1]);
}

TEST(ConjugatedTest, ClassWithoutConjReturnsTheOriginalView) {
    unconjugated_value data[] = {{1.0}, {2.0}};
    std::mdspan input(data, std::extents<int, 2>{});
    auto result = std::linalg::conjugated(input);

    static_assert(std::is_same_v<decltype(result), decltype(input)>);
    result[0].value = 3.0;
    EXPECT_DOUBLE_EQ(data[0].value, 3.0);
}

TEST(ConjugatedTest, UsesArgumentDependentConjugation) {
    using custom_conjugation::value;
    value data[] = {{1.0, 2.0}, {3.0, -4.0}};
    std::mdspan input(data, std::extents<int, 2>{});
    auto result = std::linalg::conjugated(input);

    static_assert(!std::is_same_v<decltype(result), decltype(input)>);
    EXPECT_DOUBLE_EQ(result[0].real, 1.0);
    EXPECT_DOUBLE_EQ(result[0].imaginary, -2.0);
    EXPECT_DOUBLE_EQ(result[1].real, 3.0);
    EXPECT_DOUBLE_EQ(result[1].imaginary, 4.0);
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

TEST(LayoutBlasPackedTest, DegenerateStaticSecondExtentIsAlwaysUnique) {
    using extents_t = std::extents<int, std::dynamic_extent, 1>;
    using lower_layout = std::linalg::layout_blas_packed<
        std::linalg::lower_triangle_t, std::linalg::row_major_t>;

    static_assert(lower_layout::mapping<extents_t>::is_always_unique());
    lower_layout::mapping<extents_t> mapping(extents_t{1});
    EXPECT_TRUE(mapping.is_unique());
}

TEST(LayoutBlasPackedTest, DegenerateFirstExtentIsStrided) {
    using extents_t = std::extents<int, 1, std::dynamic_extent>;
    using upper_layout = std::linalg::layout_blas_packed<
        std::linalg::upper_triangle_t, std::linalg::column_major_t>;

    static_assert(upper_layout::mapping<extents_t>::is_always_strided());
    upper_layout::mapping<extents_t> mapping(extents_t{1});
    EXPECT_TRUE(mapping.is_strided());
    EXPECT_EQ(mapping.stride(0), 1);
    EXPECT_EQ(mapping.stride(1), 1);
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
