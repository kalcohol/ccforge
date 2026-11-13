#include <gtest/gtest.h>

#include <hive>
#include <iterator>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

template<class T, class Allocator>
testing::AssertionResult structurally_valid(const std::hive<T, Allocator>& hive,
                                            std::size_t expected) {
    if (hive.size() != expected || hive.capacity() < expected)
        return testing::AssertionFailure() << "size/capacity mismatch";
    std::vector<const T*> forward;
    auto it = hive.begin();
    // Bound both traversals so a cycle fails rather than hanging this control.
    for (std::size_t index = 0; index < expected; ++index, ++it) {
        if (it == hive.end())
            return testing::AssertionFailure() << "early forward end";
        const auto* address = std::addressof(*it);
        if (std::find(forward.begin(), forward.end(), address) != forward.end())
            return testing::AssertionFailure() << "repeated element address";
        forward.push_back(address);
    }
    if (it != hive.end())
        return testing::AssertionFailure() << "forward traversal exceeds size";
    auto reverse = hive.rbegin();
    for (std::size_t index = expected; index > 0; --index, ++reverse) {
        if (reverse == hive.rend() || std::addressof(*reverse) != forward[index - 1])
            return testing::AssertionFailure() << "reverse traversal mismatch";
    }
    if (reverse != hive.rend())
        return testing::AssertionFailure() << "reverse traversal exceeds size";
    return testing::AssertionSuccess();
}

template<class T, class Allocator>
std::vector<int> payloads(const std::hive<T, Allocator>& hive) {
    std::vector<int> result;
    for (const auto& item : hive) result.push_back(item.value);
    return result;
}

const std::vector<int> sort_input{5, 1, 4, 1, 3, 0, 2};

template<class T>
struct access_allocator;

class allocator_only_value {
    template<class T> friend struct access_allocator;
    allocator_only_value(allocator_only_value&& other) noexcept
        : value(std::exchange(other.value, -1)) {}

public:
    int value;
    explicit allocator_only_value(int input) : value(input) {}
    allocator_only_value(const allocator_only_value&) = delete;
    allocator_only_value& operator=(const allocator_only_value&) = delete;
    allocator_only_value& operator=(allocator_only_value&& other) noexcept {
        value = std::exchange(other.value, -1);
        return *this;
    }
    friend void swap(allocator_only_value& left, allocator_only_value& right) noexcept {
        using std::swap;
        swap(left.value, right.value);
    }
};

template<class T>
struct access_allocator {
    using value_type = T;
    access_allocator() = default;
    template<class U> access_allocator(const access_allocator<U>&) noexcept {}
    T* allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }
    void deallocate(T* pointer, std::size_t count) noexcept {
        std::allocator<T>{}.deallocate(pointer, count);
    }
    template<class U, class... Args>
    void construct(U* pointer, Args&&... args) {
        // Construct here, not in construct_at: access is granted to this allocator.
        ::new (static_cast<void*>(pointer)) U(std::forward<Args>(args)...);
    }
    template<class U>
    bool operator==(const access_allocator<U>&) const noexcept { return true; }
};

static_assert(!std::is_move_constructible_v<allocator_only_value>);
static_assert(std::is_move_assignable_v<allocator_only_value>);
static_assert(std::is_swappable_v<allocator_only_value>);

TEST(HiveContractControls, AllocatorOnlyMoveInsertableAndAdlSwappableCanSort) {
    using value_type = allocator_only_value;
    std::hive<value_type, access_allocator<value_type>> hive;
    value_type source(7);
    // Positive allocator_traits::construct control: ordinary MoveConstructible
    // is false, but allocator-relative MoveInsertable is supported lawfully.
    auto inserted = hive.insert(std::move(source));
    EXPECT_EQ(inserted->value, 7);
    for (const int value : sort_input) hive.emplace(value);
    hive.sort([](const auto& left, const auto& right) { return left.value < right.value; });
    ASSERT_TRUE(structurally_valid(hive, sort_input.size() + 1));
    auto expected = sort_input;
    expected.push_back(7);
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(payloads(hive), expected);
}


} // namespace
