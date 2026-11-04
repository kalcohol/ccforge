#include <gtest/gtest.h>

#include <hive>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

template<class T, class Allocator>
std::vector<T> values(const std::hive<T, Allocator>& hive) {
    return {hive.begin(), hive.end()};
}

class counting_input_iterator {
public:
    using iterator_category = std::input_iterator_tag;
    using value_type = int;
    using difference_type = std::ptrdiff_t;
    using pointer = const int*;
    using reference = const int&;

    counting_input_iterator() = default;
    counting_input_iterator(std::vector<int>::const_iterator current, int* dereferences)
        : current_(current), dereferences_(dereferences) {}

    reference operator*() const {
        ++*dereferences_;
        return *current_;
    }
    pointer operator->() const { return std::addressof(*current_); }
    counting_input_iterator& operator++() {
        ++current_;
        return *this;
    }
    counting_input_iterator operator++(int) {
        auto copy = *this;
        ++*this;
        return copy;
    }
    friend bool operator==(const counting_input_iterator& left,
                           const counting_input_iterator& right) {
        return left.current_ == right.current_;
    }

private:
    std::vector<int>::const_iterator current_{};
    int* dereferences_ = nullptr;
};
static_assert(std::input_iterator<counting_input_iterator>);

TEST(HiveRanges, InputIteratorConstructionAssignAndInsertDereferenceOnce) {
    const std::vector<int> input{2, 4, 6, 8, 10};
    int constructor_dereferences = 0;
    {
        std::hive<int> hive(
            counting_input_iterator(input.begin(), &constructor_dereferences),
            counting_input_iterator(input.end(), &constructor_dereferences));
        EXPECT_EQ(values(hive), input);
    }
    EXPECT_EQ(constructor_dereferences, static_cast<int>(input.size()));

    int range_dereferences = 0;
    auto input_range = std::ranges::subrange(
        counting_input_iterator(input.begin(), &range_dereferences),
        counting_input_iterator(input.end(), &range_dereferences));
    std::hive<int> from_range(std::from_range, input_range);
    EXPECT_EQ(values(from_range), input);
    EXPECT_EQ(range_dereferences, static_cast<int>(input.size()));

    int assign_dereferences = 0;
    auto assign_input = std::ranges::subrange(
        counting_input_iterator(input.begin(), &assign_dereferences),
        counting_input_iterator(input.end(), &assign_dereferences));
    from_range.assign(assign_input.begin(), assign_input.end());
    EXPECT_EQ(values(from_range), input);
    EXPECT_EQ(assign_dereferences, static_cast<int>(input.size()));

    int assign_range_dereferences = 0;
    auto assign_range_input = std::ranges::subrange(
        counting_input_iterator(input.begin(), &assign_range_dereferences),
        counting_input_iterator(input.end(), &assign_range_dereferences));
    from_range.assign_range(assign_range_input);
    EXPECT_EQ(values(from_range), input);
    EXPECT_EQ(assign_range_dereferences, static_cast<int>(input.size()));

    int insert_dereferences = 0;
    from_range.clear();
    from_range.emplace(99);
    auto insert_input = std::ranges::subrange(
        counting_input_iterator(input.begin(), &insert_dereferences),
        counting_input_iterator(input.end(), &insert_dereferences));
    from_range.insert(insert_input.begin(), insert_input.end());
    EXPECT_EQ(from_range.size(), input.size() + 1);
    EXPECT_EQ(insert_dereferences, static_cast<int>(input.size()));

    int insert_range_dereferences = 0;
    from_range.clear();
    auto insert_range_input = std::ranges::subrange(
        counting_input_iterator(input.begin(), &insert_range_dereferences),
        counting_input_iterator(input.end(), &insert_range_dereferences));
    from_range.insert_range(insert_range_input);
    auto inserted_values = values(from_range);
    auto expected_values = input;
    std::sort(inserted_values.begin(), inserted_values.end());
    std::sort(expected_values.begin(), expected_values.end());
    EXPECT_EQ(inserted_values, expected_values);
    EXPECT_EQ(insert_range_dereferences, static_cast<int>(input.size()));
}

TEST(HiveRanges, StreamInputAndProxyRanges) {
    std::istringstream stream("1 3 5 7");
    std::ranges::istream_view<int> stream_input(stream);
    std::hive<int> hive(std::from_range, stream_input);
    EXPECT_EQ(values(hive), (std::vector<int>{1, 3, 5, 7}));

    std::vector<bool> proxy{true, false, true, true};
    std::hive<bool> bools(std::from_range, proxy);
    EXPECT_EQ(values(bools), proxy);
    bools.assign_range(proxy);
    EXPECT_EQ(values(bools), proxy);
    bools.insert_range(proxy);
    EXPECT_EQ(bools.size(), 2 * proxy.size());
    EXPECT_EQ(std::count(bools.begin(), bools.end(), true), 6);
    EXPECT_EQ(std::count(bools.begin(), bools.end(), false), 2);
    bools.insert(proxy.begin(), proxy.end());
    EXPECT_EQ(bools.size(), 3 * proxy.size());
}

TEST(HiveRanges, IteratorFromRangeAndLimitsCtad) {
    std::vector<int> input{1, 2, 3};
    constexpr auto defaults = std::hive<int>::block_capacity_default_limits();
    const std::hive_limits limits{defaults.min, defaults.min};

    std::hive iterator_deduced(input.begin(), input.end());
    static_assert(std::is_same_v<decltype(iterator_deduced), std::hive<int>>);
    EXPECT_EQ(values(iterator_deduced), input);

    std::hive iterator_allocator_deduced(
        input.begin(), input.end(), std::allocator<int>{});
    static_assert(std::is_same_v<decltype(iterator_allocator_deduced), std::hive<int>>);

    std::hive iterator_limits_deduced(input.begin(), input.end(), limits);
    static_assert(std::is_same_v<decltype(iterator_limits_deduced), std::hive<int>>);
    EXPECT_EQ(iterator_limits_deduced.block_capacity_limits().max, limits.max);

    std::hive iterator_limits_allocator_deduced(
        input.begin(), input.end(), limits, std::allocator<int>{});
    static_assert(std::is_same_v<
                  decltype(iterator_limits_allocator_deduced), std::hive<int>>);

    std::hive range_deduced(std::from_range, input);
    static_assert(std::is_same_v<decltype(range_deduced), std::hive<int>>);
    EXPECT_EQ(values(range_deduced), input);

    std::hive range_allocator_deduced(
        std::from_range, input, std::allocator<int>{});
    static_assert(std::is_same_v<decltype(range_allocator_deduced), std::hive<int>>);

    std::hive range_limits_deduced(std::from_range, input, limits);
    static_assert(std::is_same_v<decltype(range_limits_deduced), std::hive<int>>);
    EXPECT_EQ(range_limits_deduced.block_capacity_limits().max, limits.max);

    std::hive range_limits_allocator_deduced(
        std::from_range, input, limits, std::allocator<int>{});
    static_assert(std::is_same_v<
                  decltype(range_limits_allocator_deduced), std::hive<int>>);

    std::vector<bool> proxy{true, false};
    std::hive proxy_deduced(std::from_range, proxy);
    static_assert(std::is_same_v<decltype(proxy_deduced), std::hive<bool>>);
    EXPECT_EQ(values(proxy_deduced), proxy);
}

struct move_only {
    int value;
    explicit move_only(int input = 0) : value(input) {}
    move_only(const move_only&) = delete;
    move_only& operator=(const move_only&) = delete;
    move_only(move_only&&) noexcept = default;
    move_only& operator=(move_only&&) noexcept = default;
};

TEST(HiveElementCategories, MoveOnlyApplicableOperations) {
    std::hive<move_only> hive;
    hive.emplace(3);
    hive.insert(move_only{1});
    hive.emplace_hint(hive.begin(), 4);
    hive.insert(hive.begin(), move_only{2});
    EXPECT_EQ(hive.size(), 4u);

    std::vector<move_only> input;
    input.emplace_back(8);
    input.emplace_back(9);
    auto moves = std::ranges::subrange(
        std::make_move_iterator(input.begin()),
        std::make_move_iterator(input.end()));
    hive.insert_range(moves);
    EXPECT_EQ(hive.size(), 6u);

    hive.sort([](const move_only& left, const move_only& right) {
        return left.value < right.value;
    });
    ASSERT_EQ(hive.size(), 6u);
    for (auto it = std::next(hive.begin()); it != hive.end(); ++it) {
        EXPECT_LE(std::prev(it)->value, it->value);
    }

    std::hive<move_only> moved(std::move(hive));
    EXPECT_TRUE(hive.empty());
    std::hive<move_only> assigned;
    assigned = std::move(moved);
    EXPECT_TRUE(moved.empty());
    EXPECT_EQ(assigned.size(), 6u);

    std::hive<move_only> destination;
    destination.splice(assigned);
    EXPECT_TRUE(assigned.empty());
    EXPECT_EQ(destination.unique([](const move_only& current,
                                    const move_only& previous) {
        return current.value == previous.value;
    }), 0u);
    destination.clear();
    EXPECT_TRUE(destination.empty());

    std::vector<move_only> from_input;
    from_input.emplace_back(5);
    from_input.emplace_back(6);
    auto from_moves = std::ranges::subrange(
        std::make_move_iterator(from_input.begin()),
        std::make_move_iterator(from_input.end()));
    std::hive<move_only> from_range(std::from_range, from_moves);
    EXPECT_EQ(from_range.size(), 2u);
}

struct pinned {
    int value;
    explicit pinned(int input) : value(input) {}
    pinned(const pinned&) = delete;
    pinned(pinned&&) = delete;
    pinned& operator=(const pinned&) = delete;
    pinned& operator=(pinned&&) = delete;
};

TEST(HiveElementCategories, NonMovableApplicableOperationsDoNotRequireMoves) {
    std::hive<pinned> hive;
    auto first = hive.emplace(1);
    const pinned* first_address = std::addressof(*first);
    hive.emplace_hint(hive.begin(), 2);
    hive.emplace(3);
    hive.emplace(4);
    EXPECT_EQ(hive.size(), 4u);
    EXPECT_EQ(std::addressof(*first), first_address);
    hive.reserve(hive.capacity() + 16);
    EXPECT_EQ(std::addressof(*first), first_address);
    hive.trim_capacity();

    auto second = std::next(hive.begin());
    hive.erase(second);
    EXPECT_EQ(hive.size(), 3u);
    EXPECT_EQ(hive.unique([](const pinned& current, const pinned& previous) {
        return current.value == previous.value;
    }), 0u);

    std::hive<pinned> target;
    target.splice(hive);
    EXPECT_TRUE(hive.empty());
    EXPECT_EQ(target.size(), 3u);
    EXPECT_EQ(target.get_iterator(first_address), first);
    target.clear();
    EXPECT_TRUE(target.empty());
}

struct alignas(256) over_aligned {
    int value;
    explicit over_aligned(int input) : value(input) {}
};

TEST(HiveElementCategories, OverAlignedNewAndReusedSlots) {
    std::hive<over_aligned> hive;
    for (int index = 0; index < 96; ++index) hive.emplace(index);
    for (auto it = hive.begin(); it != hive.end();) {
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(std::addressof(*it)) %
                      alignof(over_aligned), 0u);
        if (it->value % 2 == 0) {
            it = hive.erase(it);
        } else {
            ++it;
        }
    }
    for (int index = 96; index < 192; ++index) hive.emplace(index);
    for (const auto& item : hive) {
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(std::addressof(item)) %
                      alignof(over_aligned), 0u);
    }
}

class tracking_resource final : public std::pmr::memory_resource {
public:
    ~tracking_resource() override = default;

    std::size_t outstanding() const { return allocations_.size(); }
    bool valid_deallocations() const { return valid_deallocations_; }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        void* const result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        allocations_.emplace(result, std::pair{bytes, alignment});
        return result;
    }
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override {
        const auto found = allocations_.find(pointer);
        if (found == allocations_.end() || found->second != std::pair{bytes, alignment}) {
            valid_deallocations_ = false;
            return;
        }
        allocations_.erase(found);
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::unordered_map<void*, std::pair<std::size_t, std::size_t>> allocations_;
    bool valid_deallocations_ = true;
};

TEST(HivePmr, AliasUsesSuppliedResourceAndReleasesAllStorage) {
    static_assert(std::is_same_v<
                  std::pmr::hive<int>,
                  std::hive<int, std::pmr::polymorphic_allocator<int>>>);
    tracking_resource resource;
    {
        std::pmr::hive<int> hive{std::pmr::polymorphic_allocator<int>{&resource}};
        ASSERT_EQ(hive.get_allocator().resource(), &resource);
        for (int index = 0; index < 256; ++index) hive.emplace(index);
        for (auto it = hive.begin(); it != hive.end();) {
            if (*it % 4 == 0) it = hive.erase(it);
            else ++it;
        }
        hive.reserve(hive.capacity() + 32);
        hive.trim_capacity();
        EXPECT_EQ(hive.size(), 192u);
        EXPECT_GT(resource.outstanding(), 0u);
    }
    EXPECT_TRUE(resource.valid_deallocations());
    EXPECT_EQ(resource.outstanding(), 0u);
}

} // namespace
