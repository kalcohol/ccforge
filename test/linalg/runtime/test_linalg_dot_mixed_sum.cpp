#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

template<class X, class Y, class T>
void expect_both_dots(X x, Y y, T init, T expected) {
    static_assert(std::is_same_v<decltype(std::linalg::dot(x, y, init)), T>);
    static_assert(std::is_same_v<decltype(std::linalg::dotc(x, y, init)), T>);
    EXPECT_EQ(std::linalg::dot(x, y, init), expected);
    EXPECT_EQ(std::linalg::dotc(x, y, init), expected);
}

struct operation_counts {
    int reads{};
    int products{};
    int conversions{};
    int conjugations{};
    int live_proxies{};
    int compound_additions{};
    int additions{};
};

template<class T>
struct numeric_proxy {
    const T* value;
    operation_counts* counts;
    mutable T product{};

    numeric_proxy(const T* v, operation_counts* c) : value(v), counts(c) {
        ++counts->live_proxies;
    }
    numeric_proxy() = delete;
    numeric_proxy(const numeric_proxy&) = delete;
    numeric_proxy(numeric_proxy&&) = delete;
    ~numeric_proxy() { --counts->live_proxies; }

    operator T() const {
        ++counts->conversions;
        return *value;
    }

    friend const T& operator*(const numeric_proxy& left, const numeric_proxy& right) {
        ++left.counts->products;
        left.product = *left.value * *right.value;
        return left.product;
    }

    friend numeric_proxy conj(const numeric_proxy& value) {
        ++value.counts->conjugations;
        return numeric_proxy(value.value, value.counts);
    }
};

template<class T>
struct numeric_proxy_accessor {
    using offset_policy = numeric_proxy_accessor;
    using element_type = const T;
    using reference = numeric_proxy<T>;
    using data_handle_type = const T*;

    operation_counts* counts{};

    reference access(data_handle_type pointer, std::size_t index) const {
        ++counts->reads;
        return reference(pointer + index, counts);
    }

    constexpr data_handle_type offset(data_handle_type pointer,
                                      std::size_t index) const noexcept {
        return pointer + index;
    }
};

struct generic_product {
    double value;
    operation_counts* counts;

    generic_product(double v, operation_counts* c) : value(v), counts(c) {}
    generic_product() = delete;
    generic_product(const generic_product&) = delete;
    generic_product(generic_product&&) = delete;

    friend generic_product operator+(const generic_product& left,
                                     const generic_product& right) {
        return generic_product(left.value + right.value, left.counts);
    }
};

struct generic_proxy {
    double value;
    operation_counts* counts;
    mutable generic_product product;

    generic_proxy(double v, operation_counts* c)
        : value(v), counts(c), product(0.0, c) {
        ++counts->live_proxies;
    }
    generic_proxy() = delete;
    generic_proxy(const generic_proxy&) = delete;
    generic_proxy(generic_proxy&&) = delete;
    ~generic_proxy() { --counts->live_proxies; }

    operator double() const {
        ++counts->conversions;
        return value;
    }

    friend const generic_product& operator*(const generic_proxy& left,
                                             const generic_proxy& right) {
        ++left.counts->products;
        left.product.value = left.value * right.value;
        return left.product;
    }

    friend generic_proxy conj(const generic_proxy& value) {
        ++value.counts->conjugations;
        return generic_proxy(value.value, value.counts);
    }
};

struct generic_proxy_accessor {
    using offset_policy = generic_proxy_accessor;
    using element_type = const double;
    using reference = generic_proxy;
    using data_handle_type = const double*;

    operation_counts* counts{};

    reference access(data_handle_type pointer, std::size_t index) const {
        ++counts->reads;
        return reference(pointer[index], counts);
    }

    constexpr data_handle_type offset(data_handle_type pointer,
                                      std::size_t index) const noexcept {
        return pointer + index;
    }
};

struct generic_sum {
    double value{};
    operation_counts* counts{};

    generic_sum& operator+=(const generic_product& product) {
        EXPECT_GT(product.counts->live_proxies, 0);
        ++counts->compound_additions;
        value += product.value;
        return *this;
    }

    friend generic_sum operator+(generic_sum left, const generic_product& right) {
        ++left.counts->additions;
        left.value += right.value;
        return left;
    }

    friend generic_sum operator+(const generic_product& left, generic_sum right) {
        return right + left;
    }

    friend generic_sum operator+(generic_sum left, const generic_sum& right) {
        ++left.counts->additions;
        left.value += right.value;
        return left;
    }
};

static_assert(!std::is_default_constructible_v<numeric_proxy<double>>);
static_assert(!std::is_copy_constructible_v<numeric_proxy<double>>);
static_assert(!std::is_move_constructible_v<numeric_proxy<double>>);
static_assert(!std::is_default_constructible_v<generic_product>);
static_assert(!std::is_copy_constructible_v<generic_product>);
static_assert(!std::is_move_constructible_v<generic_product>);

} // namespace

TEST(LinalgDotMixedSum, IntegralInitKeepsFractionalTermsUntilReturn) {
    double x_data[] = {0.5, 0.5};
    double y_data[] = {1.0, 1.0};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});

    for (int init : {0, 7, -7}) {
        SCOPED_TRACE(init);
        expect_both_dots(x, y, init, init + 1);
    }
    x_data[0] = x_data[1] = -0.5;
    for (int init : {0, 7, -7}) {
        SCOPED_TRACE(init);
        expect_both_dots(x, y, init, init - 1);
    }
}

TEST(LinalgDotMixedSum, FractionalSumConvertsOnceWithoutRounding) {
    double x_data[] = {0.75, 0.75};
    double y_data[] = {1.0, 1.0};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});
    expect_both_dots(x, y, 0, 1);
    x_data[0] = x_data[1] = -0.75;
    expect_both_dots(x, y, 0, -1);

    x_data[0] = 0.25;
    x_data[1] = 0.75;
    expect_both_dots(x, y, 0, 1);
    std::swap(x_data[0], x_data[1]);
    expect_both_dots(x, y, 0, 1);
}

TEST(LinalgDotMixedSum, NarrowFloatingInitUsesSumExpressionPrecision) {
    double x_data[] = {1.0, 1.0};
    double y_data[] = {1.0, 1.0};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});
    expect_both_dots(x, y, 0x1p24f, 0x1p24f + 2.0f);

    if constexpr (std::numeric_limits<long double>::digits >
                  std::numeric_limits<double>::digits) {
        long double wide_x_data[] = {1.0L, 1.0L};
        long double wide_y_data[] = {1.0L, 1.0L};
        std::mdspan wide_x(wide_x_data, std::extents<int, 2>{});
        std::mdspan wide_y(wide_y_data, std::extents<int, 2>{});
        expect_both_dots(wide_x, wide_y, 0x1p53, 0x1p53 + 2.0);
    }
}

TEST(LinalgDotMixedSum, NarrowComplexInitRetainsBothWideComponents) {
    using narrow = std::complex<float>;
    using wide = std::complex<double>;
    wide x_data[] = {{1.0, 1.0}, {1.0, 1.0}};
    wide y_data[] = {{1.0, 0.0}, {1.0, 0.0}};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});
    const narrow init{0x1p24f, 0x1p24f};
    const narrow expected{0x1p24f + 2.0f, 0x1p24f + 2.0f};

    EXPECT_EQ(std::linalg::dot(x, y, init), expected);
    x_data[0] = x_data[1] = wide(1.0, -1.0);
    EXPECT_EQ(std::linalg::dotc(x, y, init), expected);
}

TEST(LinalgDotMixedSum, WideComplexInitKeepsLA02ProductPrecision) {
    using narrow = std::complex<float>;
    using wide = std::complex<double>;
    narrow x_data[] = {{1.0f + 0x1p-23f, 1.0f + 0x1p-23f}};
    narrow y_data[] = {{1.0f - 0x1p-23f, 1.0f - 0x1p-23f}};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 1>{});
    const wide init{0.5, -0.25};
    const double product_component = 2.0 - 0x1p-45;

    EXPECT_EQ(std::linalg::dot(x, y, init), init + wide(0.0, product_component));
    EXPECT_EQ(std::linalg::dotc(x, y, init), init + wide(product_component, 0.0));
}

TEST(LinalgDotMixedSum, NoncopyableNumericProxyUsesEachAccessAndProductOnce) {
    double x_data[] = {0.25, 0.75};
    double y_data[] = {1.0, 1.0};
    operation_counts left, right;
    using view = std::mdspan<const double, std::extents<int, 2>,
                             std::layout_right, numeric_proxy_accessor<double>>;
    view x(x_data, view::mapping_type{}, numeric_proxy_accessor<double>{&left});
    view y(y_data, view::mapping_type{}, numeric_proxy_accessor<double>{&right});

    for (bool conjugate : {false, true}) {
        SCOPED_TRACE(conjugate);
        left = {};
        right = {};
        EXPECT_EQ(conjugate ? std::linalg::dotc(x, y, 0) : std::linalg::dot(x, y, 0), 1);
        EXPECT_EQ(left.reads, 2);
        EXPECT_EQ(right.reads, 2);
        EXPECT_EQ(left.products, 2);
        EXPECT_EQ(right.products, 0);
        EXPECT_EQ(left.conversions, 0);
        EXPECT_EQ(right.conversions, 0);
        EXPECT_EQ(left.conjugations, conjugate ? 2 : 0);
        EXPECT_EQ(left.live_proxies, 0);
        EXPECT_EQ(right.live_proxies, 0);
    }
}

TEST(LinalgDotMixedSum, ProxyValueTypeKeepsLA02WideningBeforeMultiply) {
    float x_data[] = {1.0f + 0x1p-23f};
    float y_data[] = {1.0f - 0x1p-23f};
    operation_counts left, right;
    using view = std::mdspan<const float, std::extents<int, 1>,
                             std::layout_right, numeric_proxy_accessor<float>>;
    view x(x_data, view::mapping_type{}, numeric_proxy_accessor<float>{&left});
    view y(y_data, view::mapping_type{}, numeric_proxy_accessor<float>{&right});

    for (bool conjugate : {false, true}) {
        SCOPED_TRACE(conjugate);
        left = {};
        right = {};
        EXPECT_EQ(conjugate ? std::linalg::dotc(x, y, -1.0) : std::linalg::dot(x, y, -1.0),
                  -0x1p-46);
        EXPECT_EQ(left.reads, 1);
        EXPECT_EQ(right.reads, 1);
        EXPECT_EQ(left.products, 0);
        EXPECT_EQ(left.conversions, 1);
        EXPECT_EQ(right.conversions, 1);
        EXPECT_EQ(left.conjugations, conjugate ? 1 : 0);
        EXPECT_EQ(left.live_proxies, 0);
        EXPECT_EQ(right.live_proxies, 0);
    }
}

TEST(LinalgDotMixedSum, GenericBorrowedProductRetainsLifetimeAndOperators) {
    double x_data[] = {0.25, 0.75};
    double y_data[] = {1.0, 1.0};
    operation_counts left, right;
    using view = std::mdspan<const double, std::extents<int, 2>,
                             std::layout_right, generic_proxy_accessor>;
    view x(x_data, view::mapping_type{}, generic_proxy_accessor{&left});
    view y(y_data, view::mapping_type{}, generic_proxy_accessor{&right});

    for (bool conjugate : {false, true}) {
        SCOPED_TRACE(conjugate);
        left = {};
        right = {};
        const generic_sum init{7.0, &left};
        const auto result = conjugate ? std::linalg::dotc(x, y, init)
                                      : std::linalg::dot(x, y, init);
        EXPECT_EQ(result.value, 8.0);
        EXPECT_EQ(left.reads, 2);
        EXPECT_EQ(right.reads, 2);
        EXPECT_EQ(left.products, 2);
        EXPECT_EQ(left.compound_additions, 2);
        EXPECT_EQ(left.additions, 0);
        EXPECT_EQ(left.conversions, 0);
        EXPECT_EQ(right.conversions, 0);
        EXPECT_EQ(left.conjugations, conjugate ? 2 : 0);
        EXPECT_EQ(left.live_proxies, 0);
        EXPECT_EQ(right.live_proxies, 0);
    }
}

TEST(LinalgDotMixedSum, EmptyInputReturnsUnchangedInitWithoutAccess) {
    float proxy_data[] = {1.0f};
    operation_counts counts;
    using proxy_view = std::mdspan<const float, std::extents<int, 0>,
                                   std::layout_right, numeric_proxy_accessor<float>>;
    proxy_view proxy(proxy_data, proxy_view::mapping_type{},
                     numeric_proxy_accessor<float>{&counts});
    expect_both_dots(proxy, proxy, (1 << 24) + 1, (1 << 24) + 1);
    EXPECT_EQ(counts.reads, 0);
    EXPECT_EQ(counts.products, 0);
    EXPECT_EQ(counts.conversions, 0);
    EXPECT_EQ(counts.conjugations, 0);
    EXPECT_EQ(counts.live_proxies, 0);

    double data[] = {1.0};
    std::mdspan dense(data, std::extents<int, 0>{});
    EXPECT_TRUE(std::signbit(std::linalg::dot(dense, dense, -0.0)));
    EXPECT_TRUE(std::signbit(std::linalg::dotc(dense, dense, -0.0)));
    const std::complex<float> init{-0.0f, -0.0f};
    for (const auto result : {std::linalg::dot(dense, dense, init),
                              std::linalg::dotc(dense, dense, init)}) {
        EXPECT_TRUE(std::signbit(result.real()));
        EXPECT_TRUE(std::signbit(result.imag()));
    }
}

TEST(LinalgDotMixedSum, StridedInputsAndSingleTermIncludeInitOnce) {
    double x_data[] = {0.25, 100.0, 0.75};
    double y_data[] = {1.0, 100.0, 1.0};
    using extents = std::extents<int, 2>;
    using mapping = std::layout_stride::mapping<extents>;
    const mapping map(extents{}, std::array<int, 1>{2});
    std::mdspan<double, extents, std::layout_stride> x(x_data, map);
    std::mdspan<double, extents, std::layout_stride> y(y_data, map);
    expect_both_dots(x, y, 7, 8);

    std::mdspan one_x(x_data, std::extents<int, 1>{});
    std::mdspan one_y(y_data, std::extents<int, 1>{});
    expect_both_dots(one_x, one_y, 7.0, 7.25);
}
