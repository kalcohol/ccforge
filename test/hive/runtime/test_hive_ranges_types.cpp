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

} // namespace
