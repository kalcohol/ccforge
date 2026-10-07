#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bitset>
#include <concepts>
#include <type_traits>
#include <utility>

namespace {

using namespace simd_test;
using size_type = std::simd::simd_size_type;

constexpr auto identity_map = [](auto index) { return index; };
constexpr auto exact_unary_map = [](std::same_as<size_type> auto index) { return index; };
constexpr auto exact_binary_map = [](std::same_as<size_type> auto index,
                                     std::same_as<size_type> auto size) {
    return size - 1 - index;
};
constexpr auto repeat_map = [](size_type index, size_type source_size) { return index % source_size; };

struct two_argument_preferred_map {
    constexpr size_type operator()(size_type) const noexcept { return 0; }
    constexpr size_type operator()(size_type index, size_type size) const noexcept {
        return size - 1 - index;
    }
};

struct integral_unary_floating_binary_map {
    constexpr size_type operator()(size_type) const noexcept { return 0; }
    constexpr double operator()(size_type, size_type) const noexcept { return 1.0; }
};

struct integral_unary_fractional_binary_map {
    constexpr size_type operator()(size_type) const noexcept { return 0; }
    constexpr double operator()(size_type index, size_type) const noexcept {
        return static_cast<double>(index) + 0.5;
    }
};

struct rvalue_scalar_arguments_map {
    constexpr size_type operator()(size_type&& index, size_type&& size) const noexcept {
        return size - 1 - index;
    }
};

struct lvalue_scalar_arguments_map {
    constexpr size_type operator()(size_type&) const noexcept { return 0; }
};

struct mutable_lvalue_callable_map {
    constexpr size_type operator()(size_type index) & noexcept { return index; }
};

struct cvref_callable_map {
    constexpr size_type operator()(size_type index) & noexcept { return index; }
    constexpr size_type operator()(size_type index) const& noexcept { return 3 - index; }
};

struct rvalue_callable_map {
    constexpr size_type operator()(size_type index) && noexcept { return index; }
};

struct noncopyable_map {
    noncopyable_map() = delete;
    explicit constexpr noncopyable_map(std::in_place_t) noexcept {}
    noncopyable_map(const noncopyable_map&) = delete;
    noncopyable_map(noncopyable_map&&) = delete;

    constexpr size_type operator()(size_type index) const& noexcept { return index; }
};

struct copy_tracking_map {
    int* copies;

    explicit constexpr copy_tracking_map(int* count) noexcept : copies(count) {}
    copy_tracking_map(const copy_tracking_map& other) noexcept : copies(other.copies) { ++*copies; }
    constexpr size_type operator()(size_type index) const& noexcept { return index; }
};

struct abstract_map {
    virtual void unused() const = 0;
    constexpr size_type operator()(size_type index) const& noexcept { return index; }
};

struct concrete_map : abstract_map {
    void unused() const override {}
};

struct floating_result_map {
    constexpr double operator()(size_type) const noexcept { return 0.0; }
};

struct reference_result_map {
    inline static constexpr size_type index = 0;
    constexpr const size_type& operator()(size_type) const noexcept { return index; }
};

enum class enum_index : size_type { zero };
struct enum_result_map {
    constexpr enum_index operator()(size_type) const noexcept { return enum_index::zero; }
};

struct noncallable_map {};

template<class V, class Map>
concept has_static_permute = requires(const V& values) {
    { std::simd::permute(values, std::declval<Map>()) }
        -> std::same_as<std::simd::resize_t<static_cast<size_type>(V::size), V>>;
};

template<size_type N, class V, class Map>
concept has_sized_static_permute = requires(const V& values) {
    { std::simd::permute<N>(values, std::declval<Map>()) } -> std::same_as<std::simd::resize_t<N, V>>;
};

static_assert(has_static_permute<int4, decltype(identity_map)>);
static_assert(has_static_permute<int4, decltype(exact_unary_map)>);
static_assert(has_static_permute<int4, decltype(exact_binary_map)>);
static_assert(has_static_permute<mask4, decltype(identity_map)>);
static_assert(has_static_permute<int4, two_argument_preferred_map>);
static_assert(has_static_permute<int4, integral_unary_floating_binary_map>);
static_assert(has_static_permute<int4, integral_unary_fractional_binary_map>);
static_assert(has_static_permute<int4, rvalue_scalar_arguments_map>);
static_assert(!has_static_permute<int4, lvalue_scalar_arguments_map>);
static_assert(has_static_permute<int4, mutable_lvalue_callable_map&>);
static_assert(has_static_permute<int4, mutable_lvalue_callable_map&&>);
static_assert(!has_static_permute<int4, const mutable_lvalue_callable_map&>);
static_assert(!has_static_permute<int4, rvalue_callable_map>);
static_assert(!has_static_permute<int4, floating_result_map>);
static_assert(!has_static_permute<int4, reference_result_map>);
static_assert(!has_static_permute<int4, enum_result_map>);
static_assert(!has_static_permute<int4, noncallable_map>);
static_assert(has_static_permute<int4, const noncopyable_map&>);
static_assert(has_static_permute<int4, const abstract_map&>);
static_assert(has_sized_static_permute<2, int4, decltype(exact_binary_map)>);
static_assert(has_sized_static_permute<6, int4, decltype(repeat_map)>);
static_assert(has_sized_static_permute<2, mask4, decltype(exact_binary_map)>);

constexpr bool scalar_permute_is_constexpr() {
    const int4 values([](auto lane) { return 10 + static_cast<int>(decltype(lane)::value); });
    const auto identity = std::simd::permute(values, [](auto index) { return index; });
    const auto exact = std::simd::permute(values, exact_unary_map);
    const auto reversed = std::simd::permute(values, exact_binary_map);
    const auto short_reverse = std::simd::permute<2>(values, exact_binary_map);
    const auto repeated = std::simd::permute<6>(values, repeat_map);
    const auto preferred = std::simd::permute(values, two_argument_preferred_map{});
    const auto floating_binary = std::simd::permute(values, integral_unary_floating_binary_map{});
    const auto fractional_binary = std::simd::permute(values, integral_unary_fractional_binary_map{});
    const auto prvalue_arguments = std::simd::permute(values, rvalue_scalar_arguments_map{});
    mutable_lvalue_callable_map mutable_map;
    const auto mutable_result = std::simd::permute(values, mutable_map);
    cvref_callable_map qualified_map;
    const cvref_callable_map const_qualified_map;
    const auto mutable_qualified = std::simd::permute(values, qualified_map);
    const auto const_qualified = std::simd::permute(values, const_qualified_map);
    const noncopyable_map borrowed_map{std::in_place};
    const auto borrowed_result = std::simd::permute(values, borrowed_map);
    const mask4 bits{std::bitset<4>(0b1010u)};
    const auto reversed_bits = std::simd::permute<2>(bits, exact_binary_map);
    return identity[0] == 10 && identity[3] == 13 && exact[2] == 12 &&
        reversed[0] == 13 && reversed[3] == 10 && short_reverse[0] == 13 && short_reverse[1] == 12 &&
        repeated[4] == 10 && repeated[5] == 11 && preferred[0] == 13 &&
        floating_binary[0] == 11 && floating_binary[3] == 11 && fractional_binary[3] == 13 &&
        prvalue_arguments[0] == 13 && mutable_qualified[0] == 10 && const_qualified[0] == 13 &&
        mutable_result[3] == 13 && borrowed_result[2] == 12 && reversed_bits[0] && !reversed_bits[1];
}
static_assert(scalar_permute_is_constexpr());

TEST(SimdPermuteProtocol, GenericAndExactScalarMapsAcceptStandardArguments) {
    const int4 values = load_vec<int4>(std::array<int, 4>{{10, 20, 30, 40}});
    const auto identity = std::simd::permute(values, [](auto index) { return index; });
    const auto exact = std::simd::permute(values, exact_unary_map);
    const auto reversed = std::simd::permute(values, exact_binary_map);
    for (size_type i = 0; i < int4::size; ++i) {
        EXPECT_EQ(identity[i], values[i]);
        EXPECT_EQ(exact[i], values[i]);
        EXPECT_EQ(reversed[i], values[int4::size - 1 - i]);
    }
}

TEST(SimdPermuteProtocol, TwoArgumentFormWinsIndependentlyOfTheIntegralConstraintArm) {
    const int4 values = load_vec<int4>(std::array<int, 4>{{10, 20, 30, 40}});
    const auto preferred = std::simd::permute(values, two_argument_preferred_map{});
    const auto floating_binary = std::simd::permute(values, integral_unary_floating_binary_map{});
    const auto fractional_binary = std::simd::permute(values, integral_unary_fractional_binary_map{});
    for (size_type i = 0; i < int4::size; ++i) {
        EXPECT_EQ(preferred[i], values[int4::size - 1 - i]);
        EXPECT_EQ(floating_binary[i], 20);
        EXPECT_EQ(fractional_binary[i], values[i]);
    }
}

TEST(SimdPermuteProtocol, SourceWidthIsIndependentOfTheOutputWidth) {
    const int4 values = load_vec<int4>(std::array<int, 4>{{10, 20, 30, 40}});
    const auto short_reverse = std::simd::permute<2>(values, exact_binary_map);
    EXPECT_EQ(short_reverse[0], 40);
    EXPECT_EQ(short_reverse[1], 30);
    const auto repeated = std::simd::permute<6>(values, repeat_map);
    const std::array<int, 6> expected{{10, 20, 30, 40, 10, 20}};
    for (size_type i = 0; i < 6; ++i) {
        EXPECT_EQ(repeated[i], expected[i]);
    }
    const mask4 bits{std::bitset<4>(0b1010u)};
    const auto short_bits = std::simd::permute<2>(bits, exact_binary_map);
    EXPECT_TRUE(short_bits[0]);
    EXPECT_FALSE(short_bits[1]);
}

TEST(SimdPermuteProtocol, ScalarArgumentsArePrvaluesAndTheCallableIsALvalue) {
    const int4 values = load_vec<int4>(std::array<int, 4>{{10, 20, 30, 40}});
    const auto reversed = std::simd::permute(values, rvalue_scalar_arguments_map{});
    mutable_lvalue_callable_map map;
    const auto identity = std::simd::permute(values, map);
    const auto from_rvalue = std::simd::permute(values, mutable_lvalue_callable_map{});
    cvref_callable_map qualified_map;
    const cvref_callable_map const_qualified_map;
    const auto mutable_qualified = std::simd::permute(values, qualified_map);
    const auto const_qualified = std::simd::permute(values, const_qualified_map);
    for (size_type i = 0; i < int4::size; ++i) {
        EXPECT_EQ(reversed[i], values[int4::size - 1 - i]);
        EXPECT_EQ(identity[i], values[i]);
        EXPECT_EQ(from_rvalue[i], values[i]);
        EXPECT_EQ(mutable_qualified[i], values[i]);
        EXPECT_EQ(const_qualified[i], values[3 - i]);
    }
}

TEST(SimdPermuteProtocol, TheSuppliedMapIsBorrowedWithoutCopyOrDefaultConstruction) {
    const int4 values = load_vec<int4>(std::array<int, 4>{{10, 20, 30, 40}});
    const noncopyable_map map{std::in_place};
    const auto borrowed = std::simd::permute(values, map);
    int copies = 0;
    const copy_tracking_map tracked{&copies};
    const auto tracked_result = std::simd::permute(values, tracked);
    const concrete_map concrete;
    const abstract_map& abstract = concrete;
    const auto abstract_result = std::simd::permute(values, abstract);
    EXPECT_EQ(copies, 0);
    for (size_type i = 0; i < int4::size; ++i) {
        EXPECT_EQ(borrowed[i], values[i]);
        EXPECT_EQ(tracked_result[i], values[i]);
        EXPECT_EQ(abstract_result[i], values[i]);
    }
}

TEST(SimdPermuteProtocol, ScalarMapsPreserveZeroAndUninitSentinels) {
    const int4 values = load_vec<int4>(std::array<int, 4>{{10, 20, 30, 40}});
    const auto map = [](size_type index) {
        if (index == 0) {
            return size_type{3};
        } else if (index == 1) {
            return std::simd::zero_element;
        } else if (index == 2) {
            return size_type{1};
        } else {
            return std::simd::uninit_element;
        }
    };
    const auto permuted = std::simd::permute(values, map);
    EXPECT_EQ(permuted[0], 40);
    EXPECT_EQ(permuted[1], 0);
    EXPECT_EQ(permuted[2], 20);
    const mask4 bits{std::bitset<4>(0b1010u)};
    const auto permuted_bits = std::simd::permute(bits, map);
    EXPECT_TRUE(permuted_bits[0]);
    EXPECT_FALSE(permuted_bits[1]);
    EXPECT_TRUE(permuted_bits[2]);
}

} // namespace
