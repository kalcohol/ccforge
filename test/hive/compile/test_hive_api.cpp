// Compile-only instantiation of the complete public std::hive surface.
#include <hive>

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

using hive_type = std::hive<int>;
using iterator = hive_type::iterator;
using const_iterator = hive_type::const_iterator;

static_assert(std::is_same_v<hive_type::value_type, int>);
static_assert(std::is_same_v<hive_type::allocator_type, std::allocator<int>>);
static_assert(std::is_same_v<hive_type::pointer, std::allocator_traits<std::allocator<int>>::pointer>);
static_assert(std::is_same_v<hive_type::const_pointer, std::allocator_traits<std::allocator<int>>::const_pointer>);
static_assert(std::is_same_v<hive_type::reference, int&>);
static_assert(std::is_same_v<hive_type::const_reference, const int&>);
static_assert(std::is_signed_v<hive_type::difference_type>);
static_assert(std::is_unsigned_v<hive_type::size_type>);
static_assert(std::is_same_v<hive_type::reverse_iterator, std::reverse_iterator<iterator>>);
static_assert(std::is_same_v<hive_type::const_reverse_iterator, std::reverse_iterator<const_iterator>>);
static_assert(std::bidirectional_iterator<iterator>);
static_assert(std::bidirectional_iterator<const_iterator>);
static_assert(std::convertible_to<iterator, const_iterator>);
static_assert(std::three_way_comparable<iterator, std::strong_ordering>);
static_assert(std::three_way_comparable<const_iterator, std::strong_ordering>);
static_assert(std::is_same_v<decltype(std::declval<iterator>() <=> std::declval<iterator>()),
                             std::strong_ordering>);
static_assert(std::is_same_v<decltype(std::declval<iterator>() <=> std::declval<const_iterator>()),
                             std::strong_ordering>);

static_assert(!std::default_initializable<std::hive_limits>);
static_assert(std::is_nothrow_constructible_v<std::hive_limits, std::size_t, std::size_t>);
constexpr std::hive_limits compile_limits{2, 8};
static_assert(compile_limits.min == 2 && compile_limits.max == 8);
static_assert(noexcept(hive_type()));
static_assert(noexcept(hive_type(std::declval<const std::allocator<int>&>())));
static_assert(std::is_nothrow_move_constructible_v<hive_type>);

static_assert(noexcept(std::declval<hive_type&>().begin()));
static_assert(noexcept(std::declval<const hive_type&>().begin()));
static_assert(noexcept(std::declval<hive_type&>().end()));
static_assert(noexcept(std::declval<const hive_type&>().end()));
static_assert(noexcept(std::declval<hive_type&>().rbegin()));
static_assert(noexcept(std::declval<const hive_type&>().rbegin()));
static_assert(noexcept(std::declval<hive_type&>().rend()));
static_assert(noexcept(std::declval<const hive_type&>().rend()));
static_assert(noexcept(std::declval<const hive_type&>().cbegin()));
static_assert(noexcept(std::declval<const hive_type&>().cend()));
static_assert(noexcept(std::declval<const hive_type&>().crbegin()));
static_assert(noexcept(std::declval<const hive_type&>().crend()));
static_assert(noexcept(std::declval<const hive_type&>().empty()));
static_assert(noexcept(std::declval<const hive_type&>().size()));
static_assert(noexcept(std::declval<const hive_type&>().max_size()));
static_assert(noexcept(std::declval<const hive_type&>().capacity()));
static_assert(noexcept(std::declval<const hive_type&>().get_allocator()));
static_assert(noexcept(std::declval<const hive_type&>().block_capacity_limits()));
static_assert(noexcept(hive_type::block_capacity_default_limits()));
static_assert(noexcept(hive_type::block_capacity_hard_limits()));
static_assert(noexcept(hive_type::is_within_hard_limits(compile_limits)));
static_assert(noexcept(std::declval<hive_type&>().trim_capacity()));
static_assert(noexcept(std::declval<hive_type&>().trim_capacity(1)));
static_assert(noexcept(std::declval<hive_type&>().clear()));
static_assert(noexcept(std::declval<hive_type&>().get_iterator(
    std::declval<hive_type::const_pointer>())));

constexpr auto default_limits = hive_type::block_capacity_default_limits();
constexpr auto hard_limits = hive_type::block_capacity_hard_limits();
static_assert(hive_type::is_within_hard_limits(default_limits));
static_assert(hard_limits.min <= hard_limits.max);

template<class H>
concept has_container_equality = requires(const H& left, const H& right) {
    left == right;
};
template<class I>
concept has_random_access_plus = requires(I it) { it + 1; };
static_assert(!has_container_equality<hive_type>);
static_assert(!has_random_access_plus<iterator>);

void instantiate_full_api() {
    std::allocator<int> allocator;
    constexpr auto defaults = hive_type::block_capacity_default_limits();
    const std::hive_limits limits{defaults.min, defaults.min};
    const std::vector<int> input{1, 2, 3};
    const int value = 4;
    const std::initializer_list<int> initializers{5, 6};

    hive_type default_constructed;
    hive_type allocator_constructed(allocator);
    hive_type limits_constructed(limits);
    hive_type limits_allocator_constructed(limits, allocator);
    hive_type count_constructed(3);
    hive_type count_allocator_constructed(3, allocator);
    hive_type count_limits_constructed(3, limits);
    hive_type count_limits_allocator_constructed(3, limits, allocator);
    hive_type fill_constructed(3, value);
    hive_type fill_allocator_constructed(3, value, allocator);
    hive_type fill_limits_constructed(3, value, limits);
    hive_type fill_limits_allocator_constructed(3, value, limits, allocator);
    hive_type iterator_constructed(input.begin(), input.end());
    hive_type iterator_allocator_constructed(input.begin(), input.end(), allocator);
    hive_type iterator_limits_constructed(input.begin(), input.end(), limits);
    hive_type iterator_limits_allocator_constructed(
        input.begin(), input.end(), limits, allocator);
    hive_type range_constructed(std::from_range, input);
    hive_type range_allocator_constructed(std::from_range, input, allocator);
    hive_type range_limits_constructed(std::from_range, input, limits);
    hive_type range_limits_allocator_constructed(
        std::from_range, input, limits, allocator);
    hive_type initializer_constructed(initializers);
    hive_type initializer_allocator_constructed(initializers, allocator);
    hive_type initializer_limits_constructed(initializers, limits);
    hive_type initializer_limits_allocator_constructed(initializers, limits, allocator);
    hive_type copied(iterator_constructed);
    hive_type moved(hive_type(std::from_range, input));
    hive_type copied_with_allocator(iterator_constructed, allocator);
    hive_type moved_with_allocator(hive_type(std::from_range, input), allocator);

    copied = iterator_constructed;
    moved = hive_type(std::from_range, input);
    copied = initializers;
    copied.assign(input.begin(), input.end());
    copied.assign_range(input);
    copied.assign(3, value);
    copied.assign(initializers);
    static_assert(std::is_same_v<decltype(copied.get_allocator()), std::allocator<int>>);

    auto begin = copied.begin();
    auto end = copied.end();
    auto cbegin = copied.cbegin();
    auto cend = copied.cend();
    auto rbegin = copied.rbegin();
    auto rend = copied.rend();
    auto crbegin = copied.crbegin();
    auto crend = copied.crend();
    const auto& const_copied = copied;
    (void)const_copied.begin();
    (void)const_copied.end();
    (void)const_copied.rbegin();
    (void)const_copied.rend();
    (void)begin; (void)end; (void)cbegin; (void)cend;
    (void)rbegin; (void)rend; (void)crbegin; (void)crend;

    (void)copied.empty();
    (void)copied.size();
    (void)copied.max_size();
    (void)copied.capacity();
    copied.reserve(copied.capacity() + 4);
    copied.trim_capacity();
    copied.trim_capacity(copied.size());
    copied.shrink_to_fit();
    copied.reshape(limits);
    (void)copied.block_capacity_limits();
    (void)hive_type::block_capacity_default_limits();
    (void)hive_type::block_capacity_hard_limits();
    (void)hive_type::is_within_hard_limits(limits);

    auto inserted = copied.emplace(1);
    inserted = copied.emplace_hint(copied.cbegin(), 2);
    inserted = copied.insert(value);
    inserted = copied.insert(copied.cbegin(), value);
    inserted = copied.insert(6);
    inserted = copied.insert(copied.cbegin(), 7);
    copied.insert(initializers);
    copied.insert_range(input);
    copied.insert(input.begin(), input.end());
    copied.insert(2, value);
    inserted = copied.erase(copied.cbegin());
    inserted = copied.erase(copied.cbegin(), copied.cbegin());
    copied.clear();
    copied.swap(moved);
    using std::swap;
    swap(copied, moved);

    hive_type splice_source(std::from_range, input);
    copied.splice(splice_source);
    copied.splice(hive_type(std::from_range, input));
    (void)copied.unique();
    (void)copied.unique(std::equal_to<int>{});
    copied.sort();
    copied.sort(std::less<int>{});
    if (!copied.empty()) {
        int* pointer = std::addressof(*copied.begin());
        inserted = copied.get_iterator(pointer);
        const auto& const_hive = copied;
        const_iterator const_inserted = const_hive.get_iterator(pointer);
        (void)const_inserted;
    }
    (void)inserted;
    (void)std::erase(copied, 1);
    (void)std::erase_if(copied, [](int item) { return item > 2; });

    std::pmr::hive<int> pmr_hive{std::pmr::polymorphic_allocator<int>{}};
    pmr_hive.emplace(1);
    pmr_hive.insert_range(input);
}

void instantiate_ctad() {
    std::vector<int> input{1, 2, 3};
    constexpr auto defaults = hive_type::block_capacity_default_limits();
    const std::hive_limits limits{defaults.min, defaults.min};

    std::hive a(input.begin(), input.end());
    std::hive b(input.begin(), input.end(), std::allocator<int>{});
    std::hive c(input.begin(), input.end(), limits);
    std::hive d(input.begin(), input.end(), limits, std::allocator<int>{});
    std::hive e(std::from_range, input);
    std::hive f(std::from_range, input, std::allocator<int>{});
    std::hive g(std::from_range, input, limits);
    std::hive h(std::from_range, input, limits, std::allocator<int>{});
    static_assert(std::is_same_v<decltype(a), hive_type>);
    static_assert(std::is_same_v<decltype(b), hive_type>);
    static_assert(std::is_same_v<decltype(c), hive_type>);
    static_assert(std::is_same_v<decltype(d), hive_type>);
    static_assert(std::is_same_v<decltype(e), hive_type>);
    static_assert(std::is_same_v<decltype(f), hive_type>);
    static_assert(std::is_same_v<decltype(g), hive_type>);
    static_assert(std::is_same_v<decltype(h), hive_type>);
}

int main() { return 0; }
