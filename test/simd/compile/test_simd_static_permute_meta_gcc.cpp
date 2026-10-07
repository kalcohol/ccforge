#include <simd>

#include <concepts>
#include <limits>
#include <type_traits>
#include <utility>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport static-permute meta extraction."
#endif

#ifndef FORGE_SIMD_STATIC_PERMUTE_META_REJECT
#define FORGE_SIMD_STATIC_PERMUTE_META_REJECT 0
#endif

namespace {

using size_type = std::simd::simd_size_type;
using vector4 = std::simd::vec<int, 4>;

struct borrowed_map {
    borrowed_map() = delete;
    explicit constexpr borrowed_map(std::in_place_t) noexcept {}
    borrowed_map(const borrowed_map&) = delete;
    borrowed_map(borrowed_map&&) = delete;
    constexpr size_type operator()(size_type lane) const& noexcept { return lane; }
};

struct preferred_map {
    constexpr size_type operator()(size_type) const noexcept { return 0; }
    constexpr size_type operator()(size_type&& lane, size_type&& width) const noexcept {
        return width - 1 - lane;
    }
};

struct qualified_map {
    constexpr size_type operator()(size_type lane) & noexcept { return lane; }
    constexpr size_type operator()(size_type lane) const& noexcept { return 3 - lane; }
};

consteval size_type immediate_zero() { return 0; }

struct constexpr_wrapper_map {
    constexpr size_type operator()(size_type) const noexcept { return immediate_zero(); }
};

struct copy_tracking_map {
    int* copies;
    copy_tracking_map() = delete;
    explicit constexpr copy_tracking_map(int* count) noexcept : copies(count) {}
    constexpr copy_tracking_map(const copy_tracking_map& other) noexcept : copies(other.copies) {
        ++*copies;
    }
    constexpr size_type operator()(size_type lane) const& noexcept { return lane; }
};

struct abstract_map {
    virtual void unused() const = 0;
    constexpr size_type operator()(size_type lane) const& noexcept { return lane; }
};

struct concrete_map : abstract_map {
    void unused() const override {}
};

struct rvalue_only_map {
    constexpr size_type operator()(size_type lane) && noexcept { return lane; }
};

struct lvalue_argument_map {
    constexpr size_type operator()(size_type&) const noexcept { return 0; }
};

template<class Map>
concept can_permute = requires(const vector4& values, Map&& map) {
    { std::simd::permute(values, std::forward<Map>(map)) } -> std::same_as<vector4>;
};

static_assert(!std::is_default_constructible_v<borrowed_map>);
static_assert(!std::is_copy_constructible_v<borrowed_map>);
static_assert(can_permute<const borrowed_map&>);
static_assert(can_permute<const abstract_map&>);
static_assert(!can_permute<rvalue_only_map>);
static_assert(!can_permute<lvalue_argument_map>);

constexpr bool positive_controls() {
    const vector4 values([](auto lane) { return 10 + static_cast<int>(decltype(lane)::value); });
    const borrowed_map borrowed(std::in_place);
    const auto identity = std::simd::permute(values, borrowed);
    const auto reverse = std::simd::permute(values, preferred_map{});
    qualified_map mutable_map;
    const qualified_map const_map;
    const auto mutable_result = std::simd::permute(values, mutable_map);
    const auto const_result = std::simd::permute(values, const_map);
    const auto wrapped = std::simd::permute(values, constexpr_wrapper_map{});
    int copies = 0;
    const copy_tracking_map tracking(&copies);
    const auto tracked = std::simd::permute(values, tracking);
    const auto short_result = std::simd::permute<2>(values, preferred_map{});
    const auto wider_result = std::simd::permute<6>(values,
        [](size_type lane, size_type width) { return lane % width; });
    const auto sentinel_result = std::simd::permute<2>(values,
        [](size_type) { return std::simd::zero_element; });
    const std::simd::mask<int, 4> mask(0b0101u);
    const auto reversed_mask = std::simd::permute(mask, preferred_map{});
    static_assert(decltype(short_result)::size == 2);
    static_assert(decltype(wider_result)::size == 6);
    return identity[0] == 10 && identity[3] == 13 && reverse[0] == 13 &&
        reverse[3] == 10 && mutable_result[2] == 12 && const_result[2] == 11 &&
        wrapped[3] == 10 && tracked[3] == 13 && copies == 0 &&
        short_result[0] == 13 && short_result[1] == 12 &&
        wider_result[4] == 10 && wider_result[5] == 11 &&
        sentinel_result[0] == 0 && sentinel_result[1] == 0 &&
        !reversed_mask[0] && reversed_mask[1] && !reversed_mask[2] && reversed_mask[3];
}

static_assert(positive_controls());

int abstract_control(const vector4& values) {
    const concrete_map concrete;
    const abstract_map& map = concrete;
    return std::simd::permute(values, map)[0];
}

#if FORGE_SIMD_STATIC_PERMUTE_META_REJECT == 1
struct rejected_map {
    size_type offset;
    constexpr size_type operator()(size_type lane) const noexcept { return lane + offset; }
};
#elif FORGE_SIMD_STATIC_PERMUTE_META_REJECT == 2
struct rejected_map {
    size_type operator()(size_type lane) const noexcept { return lane; }
};
#elif FORGE_SIMD_STATIC_PERMUTE_META_REJECT == 3
struct rejected_map {
    constexpr size_type operator()(size_type, size_type width) const noexcept { return width; }
};
#elif FORGE_SIMD_STATIC_PERMUTE_META_REJECT == 4
struct rejected_map {
    constexpr size_type operator()(size_type) const noexcept { return 0; }
    constexpr double operator()(size_type, size_type) const noexcept { return -0.5; }
};
#elif FORGE_SIMD_STATIC_PERMUTE_META_REJECT == 5
struct rejected_map {
    size_type offset;
    consteval size_type operator()(size_type lane) const noexcept { return lane + offset; }
};
#elif FORGE_SIMD_STATIC_PERMUTE_META_REJECT == 7
struct rejected_map {
    constexpr size_type operator()(size_type) const noexcept { return 0; }
    constexpr double operator()(size_type, size_type) const noexcept {
        return std::numeric_limits<double>::max();
    }
};
#elif FORGE_SIMD_STATIC_PERMUTE_META_REJECT == 8
struct rejected_map {
    constexpr size_type operator()(size_type) const noexcept { return -3; }
};
#endif

} // namespace

int main(int argc, char**) {
    const vector4 values(1);
#if FORGE_SIMD_STATIC_PERMUTE_META_REJECT == 0
    return abstract_control(values) == 1 ? 0 : 1;
#elif FORGE_SIMD_STATIC_PERMUTE_META_REJECT == 1 || FORGE_SIMD_STATIC_PERMUTE_META_REJECT == 5
    return std::simd::permute(values, rejected_map{argc})[0];
#elif FORGE_SIMD_STATIC_PERMUTE_META_REJECT == 6
    return std::simd::permute<-1>(values, preferred_map{})[0];
#elif FORGE_SIMD_STATIC_PERMUTE_META_REJECT >= 2 && FORGE_SIMD_STATIC_PERMUTE_META_REJECT <= 8
    return std::simd::permute(values, rejected_map{})[0];
#else
#error "Unknown static-permute negative mode."
#endif
}
