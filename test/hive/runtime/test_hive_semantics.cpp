#include <gtest/gtest.h>

#include <hive>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

template<class T, class Allocator>
std::vector<T> values(const std::hive<T, Allocator>& hive) {
    return {hive.begin(), hive.end()};
}

template<class T>
std::vector<T> sorted(std::vector<T> input) {
    std::sort(input.begin(), input.end());
    return input;
}

template<class Allocator>
auto make_ordered_hive(const std::vector<int>& input, const Allocator& allocator) {
    return std::hive<int, Allocator>(std::from_range, input, allocator);
}


TEST(HiveOperations, UniquePredicateOrderCountAndResult) {
    const std::vector<int> input{1, 1, 2, 2, 2, 1, 3, 3};
    std::hive<int> hive(std::from_range, input);
    std::vector<std::pair<int, int>> calls;
    const auto removed = hive.unique([&](const int& current, const int& previous) {
        calls.emplace_back(current, previous);
        return current == previous;
    });
    EXPECT_EQ(removed, 4u);
    EXPECT_EQ(values(hive), (std::vector<int>{1, 2, 1, 3}));
    ASSERT_EQ(calls.size(), input.size() - 1);
    EXPECT_EQ(calls.front(), (std::pair<int, int>{1, 1}));
    EXPECT_EQ(calls.back(), (std::pair<int, int>{3, 3}));

    std::hive<int> defaulted(std::from_range, input);
    EXPECT_EQ(defaulted.unique(), 4u);
    EXPECT_EQ(values(defaulted), (std::vector<int>{1, 2, 1, 3}));

    std::hive<int> empty;
    int calls_for_empty = 0;
    EXPECT_EQ(empty.unique([&](int, int) {
        ++calls_for_empty;
        return true;
    }), 0u);
    EXPECT_EQ(calls_for_empty, 0);
}


TEST(HiveOperations, SortUsesLogLinearComparisonBudget) {
    std::vector<int> input;
    for (int value = 127; value >= 0; --value) input.push_back(value);
    std::hive<int> hive(std::from_range, input);
    std::size_t comparisons = 0;
    hive.sort([&](int left, int right) {
        ++comparisons;
        return left < right;
    });
    EXPECT_TRUE(std::is_sorted(hive.begin(), hive.end()));

    std::size_t ceiling_log = 0;
    std::size_t power = 1;
    while (power < hive.size()) {
        power *= 2;
        ++ceiling_log;
    }
    const std::size_t n = hive.size();
    // Deliberately loose for an O(N log N) contract, while still rejecting a
    // quadratic comparison implementation at this input size.
    const std::size_t comparison_bound = 8 * n * ceiling_log + 8 * n + 64;
    EXPECT_GT(comparisons, 0u);
    EXPECT_LE(comparisons, comparison_bound);
}

} // namespace
