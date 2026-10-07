#include <simd>

#include <array>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's range metadata prototypes."
#endif

namespace {

using vector4 = std::simd::vec<int, 4>;
using vector2 = std::simd::vec<int, 2>;
using native_int = std::simd::basic_vec<int>;
constexpr std::size_t extent = native_int::size > 4 ? native_int::size : 4;

struct constant_view {
    int* first;

    constant_view() = delete;
    explicit constexpr constant_view(int* input) noexcept : first(input) {}
    constant_view(const constant_view&) = delete;
    constant_view(constant_view&&) = delete;
    constexpr int* begin() const noexcept { return first; }
    constexpr int* end() const noexcept { return first + 4; }
    constexpr std::size_t size() const noexcept { return 4; }
};

struct cv_view : constant_view {
    using constant_view::constant_view;
    constexpr int* end() noexcept { return first + 4; }
    constexpr int* end() const noexcept { return first + 2; }
    constexpr std::size_t size() noexcept { return 4; }
    constexpr std::size_t size() const noexcept { return 2; }
};

struct abstract_view : constant_view {
    using constant_view::constant_view;
    virtual void unused() const = 0;
};

struct stateful_view {
    int* first;
    std::size_t count;

    constexpr stateful_view(int* input, std::size_t length) noexcept
        : first(input), count(length) {}
    stateful_view(const stateful_view&) = delete;
    stateful_view(stateful_view&&) = delete;
    constexpr int* begin() const noexcept { return first; }
    constexpr int* end() const noexcept { return first + count; }
    constexpr std::size_t size() const noexcept { return count; }
};

struct private_immediate_view : stateful_view {
    using stateful_view::stateful_view;

private:
    consteval std::size_t size() const noexcept { return 4; }
};

struct nonconstexpr_view : constant_view {
    using constant_view::constant_view;
    std::size_t size() const noexcept { return 4; }
};

template<class R>
concept has_unchecked_constant = requires {
    typename std::simd::detail::unchecked_range_size_t<R>;
};

template<class R>
concept can_deduce = requires(R&& range) {
    std::simd::basic_vec{std::forward<R>(range)};
};

static_assert(!std::is_default_constructible_v<constant_view>);
static_assert(!std::is_copy_constructible_v<constant_view>);
static_assert(!std::is_move_constructible_v<constant_view>);
static_assert(std::simd::detail::constant_range_size_type<constant_view&>::value == 4);
static_assert(std::simd::detail::constant_range_size_type<const constant_view&>::value == 4);
static_assert(std::simd::detail::constant_range_size_type<constant_view&&>::value == 4);
static_assert(std::simd::detail::constant_range_size_type<const constant_view&&>::value == 4);
static_assert(std::same_as<std::simd::detail::unchecked_range_size_t<constant_view>,
    std::integral_constant<std::size_t, 4>>);
static_assert(std::simd::detail::constant_range_size_type<cv_view&>::value == 4);
static_assert(std::simd::detail::constant_range_size_type<const cv_view&>::value == 2);
static_assert(std::simd::detail::unchecked_range_size_t<cv_view>::value == 4);
static_assert(std::simd::detail::unchecked_range_size_t<const cv_view>::value == 2);
static_assert(std::is_constructible_v<vector4, const constant_view&>);
static_assert(std::is_constructible_v<vector4, cv_view&>);
static_assert(!std::is_constructible_v<vector4, const cv_view&>);
static_assert(std::is_constructible_v<vector2, const cv_view&>);
static_assert(can_deduce<const constant_view&>);
static_assert(can_deduce<const cv_view&>);
static_assert(has_unchecked_constant<const abstract_view>);
static_assert(std::simd::detail::constant_range_size_type<const abstract_view&>::value == 4);
static_assert(std::simd::detail::unchecked_range_size_t<const abstract_view>::value == 4);
static_assert(std::is_constructible_v<vector4, const abstract_view&>);
static_assert(can_deduce<const abstract_view&>);

static_assert(std::ranges::contiguous_range<stateful_view>);
static_assert(std::ranges::sized_range<stateful_view>);
static_assert(std::ranges::contiguous_range<private_immediate_view>);
static_assert(std::ranges::sized_range<private_immediate_view>);
static_assert(std::simd::detail::constant_range_size_type<stateful_view&>::value == -1);
static_assert(std::simd::detail::constant_range_size_type<const stateful_view&>::value == -1);
static_assert(!has_unchecked_constant<stateful_view>);
static_assert(!has_unchecked_constant<const stateful_view>);
static_assert(std::simd::detail::constant_range_size_type<private_immediate_view&>::value == -1);
static_assert(!has_unchecked_constant<private_immediate_view>);
static_assert(std::simd::detail::constant_range_size_type<private_immediate_view>::value == -1);
static_assert(!std::is_constructible_v<vector4, stateful_view&>);
static_assert(!std::is_constructible_v<vector4, private_immediate_view&>);
static_assert(!can_deduce<stateful_view&>);
static_assert(!can_deduce<private_immediate_view&>);
static_assert(std::simd::detail::constant_range_size_type<nonconstexpr_view&>::value == -1);
static_assert(!has_unchecked_constant<nonconstexpr_view>);
static_assert(std::simd::detail::constant_range_size_type<std::span<int>&>::value == -1);
static_assert(!has_unchecked_constant<std::span<int>>);

constexpr bool public_controls() {
    std::array<int, extent> input{};
    std::array<int, extent> output{};
    for (std::size_t i = 0; i < extent; ++i) {
        input[i] = 10 + static_cast<int>(i);
    }
    const constant_view fixed(input.data());
    const vector4 constructed(fixed);
    const std::simd::basic_vec deduced(fixed);
    const vector4 loaded = std::simd::unchecked_load<vector4>(fixed);
    stateful_view dynamic(input.data(), extent);
    private_immediate_view inaccessible(input.data(), extent);
    private_immediate_view destination(output.data(), extent);
    const auto dynamic_loaded = std::simd::unchecked_load<vector4>(dynamic);
    const auto inferred = std::simd::unchecked_load(inaccessible);
    const auto masked = std::simd::unchecked_load<vector4>(
        inaccessible, vector4::mask_type(false));
    std::simd::unchecked_store(loaded, destination);
    std::simd::unchecked_store(vector4(99), destination, vector4::mask_type(false));
    static_assert(std::same_as<std::remove_cv_t<decltype(deduced)>, vector4>);
    static_assert(std::same_as<std::remove_cv_t<decltype(inferred)>, native_int>);
    return constructed[3] == 13 && deduced[3] == 13 && loaded[3] == 13 &&
        dynamic_loaded[3] == 13 && inferred[native_int::size - 1] ==
            10 + static_cast<int>(native_int::size - 1) &&
        masked[0] == 0 && masked[3] == 0 && output[0] == 10 && output[3] == 13;
}

static_assert(public_controls());

int abstract_control(const abstract_view& input) {
    const vector4 constructed(input);
    return constructed[0] + std::simd::unchecked_load<vector4>(input)[0];
}

} // namespace

int main() { return public_controls() ? 0 : 1; }
