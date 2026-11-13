#include <gtest/gtest.h>

#include <hive>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct injected_failure {};
enum class element_operation { copy_construct, copy_assign, move_construct, move_assign, swap };

struct element_state {
    std::size_t constructed = 0;
    std::size_t destroyed = 0;
    std::size_t live = 0;
    element_operation operation = element_operation::swap;
    int budget = -1;

    void hit(element_operation current) {
        if (current != operation || budget < 0) return;
        if (budget == 0) throw injected_failure{};
        --budget;
    }
    void born() { ++constructed; ++live; }
};

// Throwing operations deliberately may change operands before throwing. All
// surviving objects remain valid; only completed constructions count as live.
template<bool AdlSwap, bool Copyable = false>
struct tracked_value {
    element_state* state;
    int value;

    tracked_value(element_state& owner, int input) : state(&owner), value(input) {
        state->born();
    }
    tracked_value(const tracked_value& other) requires Copyable
        : state(other.state), value(other.value) {
        state->hit(element_operation::copy_construct);
        state->born();
    }
    tracked_value& operator=(const tracked_value& other) requires Copyable {
        value = other.value;
        state->hit(element_operation::copy_assign);
        return *this;
    }
    tracked_value(tracked_value&& other)
        : state(other.state), value(std::exchange(other.value, -1)) {
        state->hit(element_operation::move_construct);
        state->born();
    }
    tracked_value& operator=(tracked_value&& other) {
        value = std::exchange(other.value, -1);
        state->hit(element_operation::move_assign);
        return *this;
    }
    ~tracked_value() { --state->live; ++state->destroyed; }

    friend void swap(tracked_value& left, tracked_value& right) requires AdlSwap {
        const int saved = left.value;
        left.value = right.value;
        left.state->hit(element_operation::swap);
        right.value = saved;
    }
};

static_assert(!std::is_copy_constructible_v<tracked_value<true>>);
static_assert(!std::is_copy_assignable_v<tracked_value<true>>);
static_assert(std::is_copy_constructible_v<tracked_value<false, true>>);
static_assert(std::is_copy_assignable_v<tracked_value<false, true>>);
static_assert(!std::is_nothrow_copy_constructible_v<tracked_value<false, true>>);
static_assert(!std::is_nothrow_copy_assignable_v<tracked_value<false, true>>);
static_assert(!std::is_nothrow_move_constructible_v<tracked_value<false, true>>);
static_assert(!std::is_nothrow_move_assignable_v<tracked_value<false, true>>);

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

TEST(HiveContractControls, ObservedImplementationComparatorExceptionPreservesPopulationAndLifetimes) {
    auto expected = sort_input;
    std::sort(expected.begin(), expected.end());
    std::size_t total_comparisons = 0;
    {
        element_state state;
        {
            std::hive<tracked_value<true>> hive;
            for (const int value : sort_input) hive.emplace(state, value);
            // Count every comparison on a complete, nonthrowing baseline run.
            EXPECT_NO_THROW(hive.sort([&](const auto& left, const auto& right) noexcept {
                ++total_comparisons;
                return left.value < right.value;
            }));
            ASSERT_TRUE(structurally_valid(hive, sort_input.size()));
            EXPECT_EQ(state.live, hive.size());
            EXPECT_EQ(payloads(hive), expected);
        }
        EXPECT_EQ(state.live, 0u);
        EXPECT_EQ(state.constructed, state.destroyed);
    }
    ASSERT_GT(total_comparisons, 0u);
    std::size_t observed_exceptions = 0;
    for (std::size_t throw_index = 0; throw_index < total_comparisons; ++throw_index) {
        SCOPED_TRACE(throw_index);
        element_state state; // Element operations cannot throw in this control.
        {
            // Rebuild the original input, never reuse a partially sorted attempt.
            std::hive<tracked_value<true>> hive;
            for (const int value : sort_input) hive.emplace(state, value);
            std::size_t comparisons = 0;
            bool threw = false;
            try {
                hive.sort([&](const auto& left, const auto& right) {
                    if (comparisons++ == throw_index) throw injected_failure{};
                    return left.value < right.value;
                });
            } catch (const injected_failure&) {
                threw = true;
                ++observed_exceptions;
            }
            EXPECT_TRUE(threw);
            EXPECT_EQ(comparisons, throw_index + 1);
            // BASIC GUARANTEE: unchanged size, valid traversal and live objects.
            ASSERT_TRUE(structurally_valid(hive, sort_input.size()));
            EXPECT_EQ(state.live, hive.size());
            auto actual = payloads(hive);
            if (!threw) EXPECT_TRUE(std::is_sorted(actual.begin(), actual.end()));
            std::sort(actual.begin(), actual.end());
            // OBSERVED IMPLEMENTATION property, not a public promise: retain
            // the exact multiset, including multiplicity, after comparator throws.
            EXPECT_EQ(actual, expected); // Multiplicity, not just size or retry.

            // Continued use with throwing disabled, not merely destruction.
            EXPECT_NO_THROW(hive.sort([](const auto& left, const auto& right) {
                return left.value < right.value;
            }));
            ASSERT_TRUE(structurally_valid(hive, sort_input.size()));
            EXPECT_EQ(state.live, hive.size());
            EXPECT_EQ(payloads(hive), expected); // Same observed property on retry.
            auto inserted = hive.emplace(state, 99);
            ASSERT_TRUE(structurally_valid(hive, sort_input.size() + 1));
            EXPECT_EQ(inserted->value, 99);
            EXPECT_EQ(state.live, hive.size());
            hive.erase(inserted);
            ASSERT_TRUE(structurally_valid(hive, sort_input.size()));
            EXPECT_EQ(state.live, hive.size());
            hive.clear();
            ASSERT_TRUE(structurally_valid(hive, 0));
            EXPECT_EQ(state.live, 0u);
            EXPECT_EQ(state.constructed, state.destroyed);
            hive.emplace(state, 99);
            ASSERT_TRUE(structurally_valid(hive, 1));
            EXPECT_EQ(hive.begin()->value, 99);
            EXPECT_EQ(state.live, 1u);
        }
        EXPECT_EQ(state.live, 0u);
        EXPECT_EQ(state.constructed, state.destroyed);
    }
    EXPECT_EQ(observed_exceptions, total_comparisons);
    RecordProperty("observed_comparator_exceptions", observed_exceptions);
}

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


TEST(HiveRetainedSemantics, TraversalAndIteratorComparisonOrderSurviveReuse) {
    using hive_type = std::hive<int>;
    const auto block_size = hive_type::block_capacity_default_limits().min;
    hive_type hive(std::hive_limits{block_size, block_size});
    const int initial_count = static_cast<int>(3 * block_size);
    for (int value = 0; value < initial_count; ++value) hive.emplace(value);
    for (auto it = hive.begin(); it != hive.end();) {
        if (*it / static_cast<int>(block_size) == 1 || *it % 3 != 0) it = hive.erase(it);
        else ++it;
    }
    std::vector<hive_type::iterator> retained;
    std::vector<const int*> addresses;
    std::vector<int> expected;
    for (auto it = hive.begin(); it != hive.end(); ++it) {
        retained.push_back(it);
        addresses.push_back(std::addressof(*it));
        expected.push_back(*it);
    }
    ASSERT_GE(retained.size(), 2u);
    for (int cycle = 0; cycle < 4; ++cycle) {
        SCOPED_TRACE(cycle);
        for (int value = 0; value < initial_count; ++value) hive.emplace(initial_count + value);
        ASSERT_TRUE(structurally_valid(hive, retained.size() + initial_count));
        std::vector<int> observed_retained;
        std::vector<hive_type::iterator> traversal;
        for (auto it = hive.begin(); it != hive.end(); ++it) {
            traversal.push_back(it);
            if (*it < initial_count) observed_retained.push_back(*it);
        }
        EXPECT_EQ(observed_retained, expected); // No placement rule for new elements.
        // Retain the existing traversal/comparison promise, not identity order.
        for (std::size_t i = 0; i < traversal.size(); ++i) {
            const hive_type::const_iterator left = traversal[i];
            for (std::size_t j = i + 1; j < traversal.size(); ++j) {
                EXPECT_TRUE(left < traversal[j]);
                EXPECT_EQ(traversal[j] <=> left, std::strong_ordering::greater);
            }
        }
        for (std::size_t i = 0; i < retained.size(); ++i) {
            EXPECT_EQ(*retained[i], expected[i]);
            EXPECT_EQ(std::addressof(*retained[i]), addresses[i]);
            if (i != 0) EXPECT_TRUE(retained[i - 1] < retained[i]);
        }
        for (auto it = hive.begin(); it != hive.end();) {
            if (*it >= initial_count) it = hive.erase(it);
            else ++it;
        }
        ASSERT_TRUE(structurally_valid(hive, retained.size()));
        EXPECT_EQ(std::vector<int>(hive.begin(), hive.end()), expected);
        // Do not retain an end iterator across insertion or last-element erasure.
    }
}

template<bool AdlSwap, bool Copyable = false>
void characterize_element_exception(element_operation operation) {
    int observed_exceptions = 0;
    for (const int budget : {0, 1, 4}) {
        SCOPED_TRACE(budget);
        element_state state;
        {
            std::hive<tracked_value<AdlSwap, Copyable>> hive;
            for (const int value : sort_input) hive.emplace(state, value);
            state.operation = operation;
            state.budget = budget;
            bool threw = false;
            try {
                hive.sort([](const auto& left, const auto& right) {
                    return left.value < right.value;
                });
            } catch (const injected_failure&) {
                threw = true;
                ++observed_exceptions;
            }
            state.budget = -1;
            // STRUCTURAL/lifetime characterization only after an element throw:
            // no value multiset, original addresses, retry result or rollback claim.
            ASSERT_TRUE(structurally_valid(hive, sort_input.size()));
            EXPECT_EQ(state.live, hive.size());
            if (!threw) {
                auto expected = sort_input;
                std::sort(expected.begin(), expected.end());
                EXPECT_EQ(payloads(hive), expected);
            }
            // Exercise the populated container again without constraining the
            // values left by a throwing element operation.
            auto inserted = hive.emplace(state, 99);
            ASSERT_TRUE(structurally_valid(hive, sort_input.size() + 1));
            EXPECT_EQ(inserted->value, 99);
            EXPECT_EQ(state.live, hive.size());
            hive.erase(inserted);
            ASSERT_TRUE(structurally_valid(hive, sort_input.size()));
            EXPECT_EQ(state.live, hive.size());
            hive.clear();
            ASSERT_TRUE(structurally_valid(hive, 0));
            EXPECT_EQ(state.live, 0u);
            EXPECT_EQ(state.constructed, state.destroyed);
            hive.emplace(state, 99);
            ASSERT_TRUE(structurally_valid(hive, 1));
            EXPECT_EQ(hive.begin()->value, 99);
            EXPECT_EQ(state.live, 1u);
        }
        EXPECT_EQ(state.live, 0u);
        EXPECT_EQ(state.constructed, state.destroyed);
    }
    // Zero is allowed: an implementation need not invoke these T operations.
    testing::Test::RecordProperty("observed_element_exceptions", observed_exceptions);
}

TEST(HiveSortCharacterization, DestructiveMoveConstructionStructuralLifetime) {
    characterize_element_exception<false>(element_operation::move_construct);
}

TEST(HiveSortCharacterization, DestructiveMoveAssignmentStructuralLifetime) {
    characterize_element_exception<false>(element_operation::move_assign);
}

TEST(HiveSortCharacterization, ThrowingAdlSwapStructuralLifetime) {
    characterize_element_exception<true>(element_operation::swap);
}

// Copyability does not require a sorting mechanism to copy or to throw. These
// controls retain only the structural/lifetime guarantee after element throws.
TEST(HiveSortCharacterization, CopyableThrowingCopyConstructionStructuralLifetime) {
    characterize_element_exception<false, true>(element_operation::copy_construct);
}

TEST(HiveSortCharacterization, CopyableThrowingCopyAssignmentStructuralLifetime) {
    characterize_element_exception<false, true>(element_operation::copy_assign);
}

TEST(HiveSortCharacterization, CopyableThrowingMoveConstructionStructuralLifetime) {
    characterize_element_exception<false, true>(element_operation::move_construct);
}

TEST(HiveSortCharacterization, CopyableThrowingMoveAssignmentStructuralLifetime) {
    characterize_element_exception<false, true>(element_operation::move_assign);
}

} // namespace
