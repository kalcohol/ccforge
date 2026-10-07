#include <simd>

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's constant-sized range construction."
#endif

namespace {

using vector4 = std::simd::vec<int, 4>;
using vector2 = std::simd::vec<int, 2>;
using mask4 = vector4::mask_type;
using size_type = std::simd::simd_size_type;

template<class T, std::size_t N>
struct derived_array : std::array<T, N> {};

struct member_range {
    std::array<int, 4> values{11, 22, 33, 44};
    constexpr int* begin() { return values.data(); }
    constexpr int* end() { return begin() + 4; }
    constexpr const int* begin() const { return values.data(); }
    constexpr const int* end() const { return begin() + 4; }
    constexpr std::size_t size() const { return 4; }
};

struct adl_range {
    std::array<int, 4> values{11, 22, 33, 44};
    constexpr int* begin() { return values.data(); }
    constexpr int* end() { return begin() + 4; }
    constexpr const int* begin() const { return values.data(); }
    constexpr const int* end() const { return begin() + 4; }
    friend constexpr std::size_t size(const adl_range&) { return 4; }
};

struct cv_range : member_range {
    constexpr std::size_t size() { return 4; }
    constexpr std::size_t size() const { return 2; }
    constexpr int* end() { return begin() + size(); }
    constexpr const int* end() const { return begin() + size(); }
};

struct runtime_size_range : member_range {
    std::size_t length = 4;
    constexpr std::size_t size() const { return length; }
    constexpr int* end() { return begin() + size(); }
    constexpr const int* end() const { return begin() + size(); }
};

struct const_runtime_size_range : runtime_size_range {
    constexpr std::size_t size() { return 4; }
    constexpr std::size_t size() const { return length; }
    constexpr int* end() { return begin() + size(); }
    constexpr const int* end() const { return begin() + size(); }
};

struct nonconstexpr_size_range : member_range {
    std::size_t size() const { return 4; }
};

struct noncontiguous_range : member_range {
    constexpr auto begin() { return values.rbegin(); }
    constexpr auto end() { return values.rend(); }
    constexpr auto begin() const { return values.rbegin(); }
    constexpr auto end() const { return values.rend(); }
};

struct borrowed_range {
    const int* first;
    borrowed_range() = delete;
    explicit constexpr borrowed_range(const int* input) : first(input) {}
    borrowed_range(const borrowed_range&) = delete;
    borrowed_range(borrowed_range&&) = delete;
    constexpr const int* begin() const { return first; }
    constexpr const int* end() const { return first + 4; }
    constexpr std::size_t size() const { return 4; }
};

struct tracked_range {
    const int* first;
    int* copies;
    tracked_range(const int* input, int* counter) : first(input), copies(counter) {}
    tracked_range(const tracked_range& other) : first(other.first), copies(other.copies) {
        ++*copies;
    }
    const int* begin() const { return first; }
    const int* end() const { return first + 4; }
    constexpr std::size_t size() const { return 4; }
};

struct abstract_range : borrowed_range {
    using borrowed_range::borrowed_range;
    virtual void unused() const = 0;
};

struct concrete_range : abstract_range {
    using abstract_range::abstract_range;
    void unused() const override {}
};

template<class R>
concept can_deduce = requires(R&& range) {
    std::simd::basic_vec{std::forward<R>(range)};
};

template<class R>
concept can_deduce_masked = requires(R&& range, const mask4& mask) {
    std::simd::basic_vec{std::forward<R>(range), mask};
};

static_assert(std::ranges::contiguous_range<member_range>);
static_assert(std::ranges::sized_range<member_range>);
static_assert(std::ranges::contiguous_range<const adl_range>);
static_assert(std::ranges::sized_range<const adl_range>);
static_assert(std::ranges::contiguous_range<const borrowed_range>);
static_assert(std::ranges::sized_range<const borrowed_range>);
static_assert(std::ranges::contiguous_range<const abstract_range>);
static_assert(std::ranges::sized_range<const abstract_range>);

static_assert(std::is_constructible_v<vector4, derived_array<int, 4>&>);
static_assert(std::is_constructible_v<vector4, const derived_array<int, 4>&, mask4>);
static_assert(std::is_constructible_v<vector4, member_range&>);
static_assert(std::is_constructible_v<vector4, const member_range&>);
static_assert(std::is_constructible_v<vector4, member_range&&>);
static_assert(std::is_constructible_v<vector4, const member_range&&>);
static_assert(std::is_constructible_v<vector4, const adl_range&>);
static_assert(std::is_constructible_v<vector4, const borrowed_range&>);
static_assert(std::is_constructible_v<vector4, const abstract_range&>);
static_assert(can_deduce<derived_array<int, 4>&>);
static_assert(can_deduce<const member_range&>);
static_assert(can_deduce<member_range&&>);
static_assert(can_deduce<const member_range&&>);
static_assert(can_deduce<const adl_range&>);
static_assert(can_deduce<const borrowed_range&>);
static_assert(can_deduce<const abstract_range&>);
static_assert(can_deduce_masked<const derived_array<int, 4>&>);
static_assert(can_deduce_masked<const member_range&>);
static_assert(can_deduce_masked<const adl_range&>);

static_assert(std::is_constructible_v<vector4, cv_range&>);
static_assert(!std::is_constructible_v<vector4, const cv_range&>);
static_assert(std::is_constructible_v<vector2, const cv_range&>);
static_assert(!std::is_constructible_v<vector2, cv_range&>);
static_assert(std::same_as<decltype(std::simd::basic_vec{std::declval<cv_range&>()}), vector4>);
static_assert(std::same_as<decltype(std::simd::basic_vec{std::declval<const cv_range&>()}), vector2>);
static_assert(std::is_constructible_v<vector4, const_runtime_size_range&>);
static_assert(!std::is_constructible_v<vector4, const const_runtime_size_range&>);
static_assert(!can_deduce<const const_runtime_size_range&>);

// A constexpr size() which reads object state is not a constant expression
// for an unknown constructor argument, even if the default object has size 4.
static_assert(std::ranges::contiguous_range<runtime_size_range>);
static_assert(std::ranges::sized_range<runtime_size_range>);
static_assert(!std::is_constructible_v<vector4, runtime_size_range&>);
static_assert(!std::is_constructible_v<vector4, nonconstexpr_size_range&>);
static_assert(!std::is_constructible_v<vector4, std::vector<int>&>);
static_assert(!std::is_constructible_v<vector4, std::span<const int>>);
static_assert(!can_deduce<runtime_size_range&>);
static_assert(!can_deduce<nonconstexpr_size_range&>);
static_assert(!can_deduce<std::vector<int>&>);
static_assert(!can_deduce<std::span<const int>>);
static_assert(std::ranges::sized_range<noncontiguous_range>);
static_assert(!std::ranges::contiguous_range<noncontiguous_range>);
static_assert(!std::is_constructible_v<vector4, noncontiguous_range&>);
static_assert(!can_deduce<noncontiguous_range&>);
static_assert(!can_deduce<derived_array<long double, 4>&>);
static_assert(!std::is_constructible_v<vector4, derived_array<int, 3>&>);
static_assert(!std::is_constructible_v<vector4, derived_array<int, 5>&, mask4>);
static_assert(!can_deduce<derived_array<int, 0>&>);
static_assert(!can_deduce<derived_array<int, 65>&>);

static_assert(std::is_constructible_v<vector4, int (&)[4]>);
static_assert(std::is_constructible_v<vector4, const std::array<int, 4>&>);
static_assert(std::is_constructible_v<vector4, std::span<const int, 4>>);
static_assert(can_deduce<int (&)[4]>);
static_assert(can_deduce<const std::array<int, 4>&>);
static_assert(can_deduce<std::span<const int, 4>>);
static_assert(std::is_constructible_v<std::simd::vec<int, 1>, std::ranges::single_view<int>&>);
static_assert(can_deduce<std::ranges::single_view<int>&>);

constexpr mask4 selected_lanes() {
    return mask4([](auto lane) { return decltype(lane)::value % 2 == 0; });
}

constexpr bool constant_construction() {
    const derived_array<int, 4> derived{{{11, 22, 33, 44}}};
    const member_range member;
    const adl_range adl;
    const borrowed_range borrowed(member.values.data());
    const vector4 from_derived(derived);
    const vector4 from_member(member, selected_lanes());
    const std::simd::basic_vec from_adl(adl);
    const std::simd::basic_vec from_borrowed(borrowed, selected_lanes());
    cv_range cv;
    const std::simd::basic_vec mutable_cv(cv);
    const std::simd::basic_vec const_cv(std::as_const(cv));
    return from_derived[3] == 44 && from_member[0] == 11 && from_member[1] == 0 &&
        from_adl[2] == 33 && from_borrowed[2] == 33 && from_borrowed[3] == 0 &&
        mutable_cv[3] == 44 && const_cv[1] == 22;
}

static_assert(constant_construction());

template<class V>
void expect_lanes(const V& value, const std::array<typename V::value_type, V::size>& expected) {
    for (size_type lane = 0; lane < V::size; ++lane) {
        EXPECT_EQ(value[lane], expected[static_cast<std::size_t>(lane)]);
    }
}

TEST(SimdStaticRangeConstruction, DerivedArraySupportsConstructionAndMaskedDeduction) {
    const derived_array<int, 4> input{{{11, 22, 33, 44}}};
    const vector4 direct(input);
    const std::simd::basic_vec deduced(input);
    const std::simd::basic_vec masked(input, selected_lanes());
    expect_lanes(direct, {11, 22, 33, 44});
    expect_lanes(deduced, {11, 22, 33, 44});
    expect_lanes(masked, {11, 0, 33, 0});
}

TEST(SimdStaticRangeConstruction, MemberAndAdlSizesSupportBothConstructors) {
    const member_range member;
    const adl_range adl;
    const std::simd::basic_vec from_member(member);
    const vector4 from_adl(adl);
    const vector4 masked_member(member, selected_lanes());
    const std::simd::basic_vec masked_adl(adl, selected_lanes());
    expect_lanes(from_member, {11, 22, 33, 44});
    expect_lanes(from_adl, {11, 22, 33, 44});
    expect_lanes(masked_member, {11, 0, 33, 0});
    expect_lanes(masked_adl, {11, 0, 33, 0});
}

TEST(SimdStaticRangeConstruction, SizeQueryPreservesTheNamedRangesCvQualification) {
    cv_range input;
    const std::simd::basic_vec mutable_range(input);
    const std::simd::basic_vec const_range(std::as_const(input));
    expect_lanes(mutable_range, {11, 22, 33, 44});
    expect_lanes(const_range, {11, 22});
}

TEST(SimdStaticRangeConstruction, SizeDetectionDoesNotConstructOrCopyTheRange) {
    const std::array<int, 4> input{11, 22, 33, 44};
    const borrowed_range borrowed(input.data());
    const vector4 from_borrowed(borrowed);
    int copies = 0;
    const tracked_range tracked(input.data(), &copies);
    const std::simd::basic_vec from_tracked(tracked);
    const concrete_range concrete(input.data());
    const abstract_range& abstract = concrete;
    const std::simd::basic_vec from_abstract(abstract);
    EXPECT_EQ(copies, 0);
    expect_lanes(from_borrowed, input);
    expect_lanes(from_tracked, input);
    expect_lanes(from_abstract, input);
}

TEST(SimdStaticRangeConstruction, ConversionFlagsAndExistingRangeKindsRemainAvailable) {
    const derived_array<double, 4> input{{{1.5, 2.5, 3.5, 4.5}}};
    using float4 = std::simd::vec<float, 4>;
    const float4 converted(input, std::simd::flag_convert);
    const float4 masked(input, float4::mask_type(selected_lanes()), std::simd::flag_convert);
    expect_lanes(converted, {1.5f, 2.5f, 3.5f, 4.5f});
    expect_lanes(masked, {1.5f, 0.0f, 3.5f, 0.0f});
    const int array[4]{11, 22, 33, 44};
    const std::array<int, 4> standard{11, 22, 33, 44};
    const std::span<const int, 4> span(standard);
    const std::simd::basic_vec from_array(array);
    const std::simd::basic_vec from_standard(standard);
    const std::simd::basic_vec from_span(span);
    expect_lanes(from_array, standard);
    expect_lanes(from_standard, standard);
    expect_lanes(from_span, standard);
}

} // namespace
