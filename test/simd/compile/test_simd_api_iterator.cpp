#include "simd_test_common.hpp"

#include <compare>

namespace {

using namespace simd_test;

constexpr bool constexpr_iterator_spaceship() {
    int4 value(0);
    auto first = value.begin();
    auto second = first + 1;

    return (first <=> first) == 0 &&
        (first <=> second) < 0 &&
        (second <=> first) > 0;
}

static_assert(std::is_same<decltype(std::declval<int4&>().begin()), typename int4::iterator>::value,
    "begin() should return iterator");
static_assert(requires(typename int4::iterator it) { it <=> it; },
    "simd_iterator should expose operator<=>");
static_assert(std::is_same<
    decltype(std::declval<typename int4::iterator>() <=> std::declval<typename int4::iterator>()),
    std::strong_ordering>::value,
    "simd_iterator ordering should be strong_ordering");
static_assert(constexpr_iterator_spaceship(),
    "simd_iterator operator<=> should be constexpr-capable");
static_assert(std::is_same<decltype(std::declval<typename int4::iterator&>()++), typename int4::iterator>::value,
    "iterator post-increment should return iterator");
static_assert(std::is_same<decltype(std::declval<typename int4::iterator&>()--), typename int4::iterator>::value,
    "iterator post-decrement should return iterator");
static_assert(std::is_same<typename int4::iterator::reference, int>::value,
    "iterator::reference should be the scalar value type");
static_assert(std::is_same<typename int4::const_iterator::reference, int>::value,
    "const_iterator::reference should be the scalar value type");
static_assert(std::is_same<typename int4::iterator::iterator_category, std::input_iterator_tag>::value,
    "iterator_category should remain input_iterator_tag for legacy algorithms");
static_assert(std::is_same<typename int4::iterator::iterator_concept, std::random_access_iterator_tag>::value,
    "iterator_concept should model random access traversal");
static_assert(std::is_same<decltype(std::declval<const int4&>().begin()), typename int4::const_iterator>::value,
    "const begin() should return const_iterator");
static_assert(std::is_same<decltype(std::declval<const int4&>().cbegin()), typename int4::const_iterator>::value,
    "cbegin() should return const_iterator");
static_assert(std::is_same<decltype(std::declval<int4&>().end()), std::default_sentinel_t>::value,
    "end() should return default_sentinel_t");
static_assert(std::is_same<decltype(std::declval<const int4&>().cend()), std::default_sentinel_t>::value,
    "cend() should return default_sentinel_t");
static_assert(std::is_same<decltype(std::declval<int4&>().end() - std::declval<int4&>().begin()), std::simd::simd_size_type>::value,
    "default sentinel difference should use simd_size_type");
static_assert(std::is_same<decltype(std::declval<int4&>().begin() - std::declval<int4&>().end()), std::simd::simd_size_type>::value,
    "iterator minus default sentinel should use simd_size_type");

} // namespace

int main() {
    return 0;
}
