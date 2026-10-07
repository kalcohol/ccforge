#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>

#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

struct matrix2 {
    int a{};
    int b{};
    int c{};
    int d{};

    friend constexpr matrix2 operator*(const matrix2& left, const matrix2& right) {
        return {left.a * right.a + left.b * right.c,
                left.a * right.b + left.b * right.d,
                left.c * right.a + left.d * right.c,
                left.c * right.b + left.d * right.d};
    }

    matrix2& operator*=(const matrix2& right) {
        *this = *this * right;
        return *this;
    }

    friend constexpr bool operator==(const matrix2&, const matrix2&) = default;
};

struct scale_calls {
    int accesses{};
    int products{};
    int assignments{};
};

struct scale_proxy {
    matrix2* value;
    scale_calls* observations;

    scale_proxy(matrix2* input, scale_calls* count) : value(input), observations(count) {}
    scale_proxy(const scale_proxy&) = delete;
    scale_proxy(scale_proxy&&) = delete;

    operator matrix2() const { return *value; }

    scale_proxy&& operator=(const matrix2& input) && {
        ++observations->assignments;
        *value = input;
        return std::move(*this);
    }

    scale_proxy&& operator*=(const matrix2& factor) && {
        ++observations->products;
        return std::move(*this) = *value * factor;
    }

    friend matrix2 operator*(const matrix2& factor, scale_proxy&& input) {
        ++input.observations->products;
        return factor * *input.value;
    }
};

struct scale_accessor {
    using offset_policy = scale_accessor;
    using element_type = matrix2;
    using reference = scale_proxy;
    using data_handle_type = matrix2*;

    scale_calls* observations{};

    reference access(data_handle_type pointer, std::size_t index) const {
        ++observations->accesses;
        return {pointer + index, observations};
    }

    data_handle_type offset(data_handle_type pointer, std::size_t index) const {
        return pointer + index;
    }
};

static_assert(std::semiregular<matrix2>);
static_assert(std::semiregular<scale_accessor>);
static_assert(std::common_reference_with<scale_proxy&&, matrix2&>);
static_assert(std::is_assignable_v<scale_proxy, matrix2>);

} // namespace

TEST(LinalgScaleOrdering, NoncommutativeVectorAndMatrixElementsUseLeftFactor) {
    const matrix2 alpha{1, 1, 0, 1};
    matrix2 vector_data[] = {{1, 0, 1, 1}, {1, 0, 0, 1}};
    std::mdspan vector(vector_data, std::extents<int, 2>{});
    std::linalg::scale(alpha, vector);
    EXPECT_EQ(vector_data[0], (matrix2{2, 1, 1, 1}));
    EXPECT_EQ(vector_data[1], alpha);

    matrix2 matrix_data[] = {
        {1, 0, 1, 1}, {1, 0, 0, 1}, {2, 0, 0, 3}, {0, 1, 1, 0}};
    std::mdspan matrix(matrix_data, std::extents<int, 2, 2>{});
    std::linalg::scale(alpha, matrix);
    EXPECT_EQ(matrix_data[0], (matrix2{2, 1, 1, 1}));
    EXPECT_EQ(matrix_data[1], alpha);
    EXPECT_EQ(matrix_data[2], (matrix2{2, 3, 0, 3}));
    EXPECT_EQ(matrix_data[3], (matrix2{1, 1, 1, 0}));
}

TEST(LinalgScaleOrdering, RvalueWritableProxyAccessesEachMappedElementOnce) {
    const matrix2 alpha{1, 1, 0, 1};
    const matrix2 gap{9, 8, 7, 6};
    std::array data{matrix2{1, 0, 1, 1}, gap, matrix2{2, 0, 0, 3}, gap};
    scale_calls observations;
    using extents = std::extents<int, 2>;
    std::layout_stride::mapping<extents> mapping(extents{}, std::array<int, 1>{2});
    std::mdspan<matrix2, extents, std::layout_stride, scale_accessor> vector(
        data.data(), mapping, scale_accessor{&observations});

    std::linalg::scale(alpha, vector);
    EXPECT_EQ(data[0], (matrix2{2, 1, 1, 1}));
    EXPECT_EQ(data[2], (matrix2{2, 3, 0, 3}));
    EXPECT_EQ(data[1], gap);
    EXPECT_EQ(data[3], gap);
    EXPECT_EQ(observations.accesses, 2);
    EXPECT_EQ(observations.products, 2);
    EXPECT_EQ(observations.assignments, 2);
}

TEST(LinalgScaleOrdering, ArithmeticVectorBlocksTailAndMatrixControls) {
    std::array<float, 257> data{};
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<float>(static_cast<int>(i) - 128);
    }
    std::mdspan vector(data.data(), std::extents<int, 257>{});
    std::linalg::scale(-0.5f, vector);
    for (std::size_t i = 0; i < data.size(); ++i) {
        EXPECT_FLOAT_EQ(data[i], -0.5f * static_cast<float>(static_cast<int>(i) - 128));
    }

    double matrix_data[] = {1.0, -2.0, 3.0, -4.0};
    std::mdspan matrix(matrix_data, std::extents<int, 2, 2>{});
    std::linalg::scale(2.0, matrix);
    EXPECT_DOUBLE_EQ(matrix_data[0], 2.0);
    EXPECT_DOUBLE_EQ(matrix_data[1], -4.0);
    EXPECT_DOUBLE_EQ(matrix_data[2], 6.0);
    EXPECT_DOUBLE_EQ(matrix_data[3], -8.0);
}
