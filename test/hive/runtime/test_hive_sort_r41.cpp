#include <gtest/gtest.h>

#include <hive>

#include "hive_test_allocator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <random>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace {

using forge_hive_allocator_test::allocation_state;
template<class T>
using allocator = forge_hive_allocator_test::stateful_allocator<T>;
template<class T>
using hive_type = std::hive<T, allocator<T>>;

struct multifield_value {
    int key;
    int serial;
    std::uint32_t payload;
};

struct assignment_template_called {};

struct assignment_template_value {
    int key;
    int serial;
    std::uint32_t payload;
    inline static std::size_t template_assignments = 0;

    template<class U> requires std::is_same_v<std::remove_cvref_t<U>, assignment_template_value>
    assignment_template_value& operator=(U&&) {
        ++template_assignments;
        throw assignment_template_called{};
    }
};

struct boundary_value {
    int key;
    int serial;
    std::uint64_t payload;
};

struct generic_value {
    int key;
    int serial;
    std::array<std::uint64_t, 7> payload;
};

template<class T>
constexpr bool trivial_copy = std::is_trivially_copyable_v<T> &&
                              std::is_trivially_copy_constructible_v<T> &&
                              std::is_trivially_copy_assignable_v<T>;
static_assert(trivial_copy<multifield_value>);
static_assert(trivial_copy<assignment_template_value>);
static_assert(trivial_copy<boundary_value>);
static_assert(sizeof(boundary_value) == 16);
static_assert(sizeof(generic_value) == 64);
static_assert(hive_type<multifield_value>::__test_sort_uses_value_buffer());
static_assert(hive_type<assignment_template_value>::__test_sort_uses_value_buffer());
static_assert(hive_type<boundary_value>::__test_sort_uses_value_buffer());
static_assert(!hive_type<generic_value>::__test_sort_uses_value_buffer());

enum class input_order { random, sorted, reverse, duplicates };
enum class topology { dense, sparse, churn };

constexpr std::array sizes{0u, 1u, 2u, 7u, 65u, 4096u};
constexpr std::array orders{input_order::random, input_order::sorted,
                            input_order::reverse, input_order::duplicates};
constexpr std::array shapes{topology::dense, topology::sparse, topology::churn};

template<class T>
void set_value(T& value, int key, int serial) {
    value.key = key;
    value.serial = serial;
    const auto seed = static_cast<std::uint64_t>(serial) * 0x9e3779b97f4a7c15ULL;
    if constexpr (std::is_same_v<T, generic_value>) {
        for (std::size_t i = 0; i < value.payload.size(); ++i) value.payload[i] = seed + i;
    } else {
        value.payload = static_cast<decltype(value.payload)>(seed);
    }
}

template<class H>
void populate(H& hive, std::size_t n, input_order order, topology shape) {
    const auto extra = shape == topology::dense || n < 3 ? 0 : n / 2 + 1;
    for (std::size_t i = 0; i < n + extra; ++i) hive.emplace();
    if (shape == topology::churn) {
        for (int round = 0; round < 2; ++round) {
            std::size_t index = 0;
            std::size_t removed = 0;
            for (auto it = hive.begin(); it != hive.end();) {
                if (index++ % 3 == 1) { it = hive.erase(it); ++removed; }
                else ++it;
            }
            for (std::size_t i = 0; i < removed; ++i) hive.emplace();
        }
    }
    std::size_t removed = 0;
    std::size_t index = 0;
    for (auto it = hive.begin(); it != hive.end() && removed < extra;) {
        if (index++ % 2 == 0) { it = hive.erase(it); ++removed; }
        else ++it;
    }

    // Form holes first, then set the final traversal sequence and distinct payloads.
    std::mt19937_64 rng(1);
    index = 0;
    for (auto& value : hive) {
        int key = 0;
        switch (order) {
        case input_order::random: key = static_cast<int>(rng() % (n + 17)); break;
        case input_order::sorted: key = static_cast<int>(index); break;
        case input_order::reverse: key = static_cast<int>(n - index); break;
        case input_order::duplicates: key = static_cast<int>(rng() % 5); break;
        }
        set_value(value, key, static_cast<int>(++index));
    }
    EXPECT_EQ(hive.size(), n);
}

template<class H, class Projection>
auto collect(const H& hive, Projection projection) {
    using R = decltype(projection(std::declval<const typename H::value_type&>()));
    std::vector<R> result;
    auto it = hive.begin();
    for (std::size_t i = 0; i < hive.size() && it != hive.end(); ++i, ++it) {
        result.push_back(projection(*it));
    }
    EXPECT_EQ(it, hive.end());
    EXPECT_EQ(result.size(), hive.size());
    auto reverse = hive.end();
    for (std::size_t i = result.size(); i > 0; --i) {
        if (reverse == hive.begin()) {
            ADD_FAILURE() << "Reverse traversal ended early";
            break;
        }
        --reverse;
        EXPECT_EQ(projection(*reverse), result[i - 1]);
    }
    EXPECT_EQ(reverse, hive.begin());
    return result;
}

template<class H>
auto contents(const H& hive) {
    return collect(hive, [](const auto& value) {
        return std::tuple(value.key, value.serial, value.payload);
    });
}

template<class H>
auto slots(const H& hive) {
    return collect(hive, [](const auto& value) -> const void* { return std::addressof(value); });
}

template<class H>
struct observation {
    std::size_t size;
    std::size_t capacity;
    decltype(contents(std::declval<const H&>())) values;
    std::vector<const void*> addresses;
    decltype(allocation_state::allocations) ledger;

    observation(const H& hive, const allocation_state& state)
        : size(hive.size()), capacity(hive.capacity()), values(contents(hive)),
          addresses(slots(hive)), ledger(state.allocations) {}

    void expect_preserved(const H& hive, const allocation_state& state) const {
        EXPECT_EQ(hive.size(), size);
        EXPECT_EQ(hive.capacity(), capacity);
        EXPECT_EQ(slots(hive), addresses);
        auto actual = contents(hive);
        auto expected = values;
        std::sort(actual.begin(), actual.end());
        std::sort(expected.begin(), expected.end());
        EXPECT_EQ(actual, expected);
        EXPECT_TRUE(state.valid_deallocations);
        ASSERT_EQ(state.allocations.size(), ledger.size());
        for (const auto& [pointer, record] : ledger) {
            const auto found = state.allocations.find(pointer);
            ASSERT_NE(found, state.allocations.end());
            EXPECT_EQ(found->second.bytes, record.bytes);
            EXPECT_EQ(found->second.alignment, record.alignment);
            EXPECT_EQ(found->second.owner_id, record.owner_id);
            EXPECT_EQ(found->second.allocated_type, record.allocated_type);
        }
    }

    void expect_sorted(const H& hive) const {
        std::vector<int> expected_keys;
        for (const auto& value : values) expected_keys.push_back(std::get<0>(value));
        std::sort(expected_keys.begin(), expected_keys.end());
        EXPECT_EQ(collect(hive, [](const auto& value) { return value.key; }), expected_keys);
    }
};

std::size_t comparison_budget(std::size_t n) {
    std::size_t log = 0;
    for (std::size_t remaining = n > 0 ? n - 1 : 0; remaining != 0; remaining >>= 1) ++log;
    return 8 * n * log + 8 * n + 64; // R4 F5: ceil(log2(n)), including n = 0 and 1.
}

enum class comparison_kind {
    reflexive, always_true, always_false, seeded_random,
    cyclic, incomparable, changing, throwing
};
constexpr std::array invalid_kinds{
    comparison_kind::reflexive, comparison_kind::always_true,
    comparison_kind::always_false, comparison_kind::seeded_random,
    comparison_kind::cyclic, comparison_kind::incomparable,
    comparison_kind::changing, comparison_kind::throwing
};
constexpr auto no_throw = std::numeric_limits<std::size_t>::max();

struct comparison_failure { std::size_t position; const void* owner; };
struct budget_exceeded {};

struct comparison_state {
    std::size_t calls = 0;
    std::size_t budget;
    std::size_t throw_at = no_throw;
    const void* first_instance = nullptr;
    bool same_instance = true;
    std::mt19937_64 random{1};
};

template<class T>
struct comparator {
    comparison_state& state;
    comparison_kind kind;

    bool operator()(const T& left, const T& right) {
        if (state.first_instance == nullptr) state.first_instance = this;
        state.same_instance = state.same_instance && state.first_instance == this;
        const auto position = state.calls++;
        // Fail a runaway comparison sequence without relaxing the F5 assertion.
        if (state.calls > state.budget) throw budget_exceeded{};
        if (kind == comparison_kind::throwing && position == state.throw_at) {
            throw comparison_failure{position, &state};
        }
        switch (kind) {
        case comparison_kind::reflexive: return left.key <= right.key;
        case comparison_kind::always_true: return true;
        case comparison_kind::always_false: return false;
        case comparison_kind::seeded_random: return (state.random() & 1) != 0;
        case comparison_kind::cyclic: return (left.key % 3 + 1) % 3 == right.key % 3;
        case comparison_kind::incomparable:
            return left.key % 5 != 0 && right.key % 5 != 0 && left.key < right.key;
        case comparison_kind::changing:
            return position % 2 == 0 ? left.key < right.key : left.key >= right.key;
        case comparison_kind::throwing: return left.key < right.key;
        }
        return false;
    }
};

template<class H>
std::size_t checked_sort(H& hive, comparison_kind kind = comparison_kind::throwing,
                         std::size_t throw_at = no_throw, bool expect_throw = false) {
    comparison_state state{0, comparison_budget(hive.size()), throw_at};
    comparator<typename H::value_type> compare{state, kind};
    bool threw = false;
    try {
        hive.sort(std::ref(compare));
    } catch (const comparison_failure& failure) {
        threw = true;
        EXPECT_EQ(kind, comparison_kind::throwing);
        EXPECT_EQ(failure.position, throw_at);
        EXPECT_EQ(failure.owner, &state);
    } catch (const budget_exceeded&) {
        ADD_FAILURE() << "Sort exceeded the F5 comparison bound";
    } catch (...) {
        ADD_FAILURE() << "Sort propagated an unexpected exception";
    }
    EXPECT_EQ(threw, expect_throw);
    EXPECT_LE(state.calls, state.budget);
    EXPECT_TRUE(state.same_instance);
    if (expect_throw) EXPECT_EQ(state.calls, throw_at + 1);
    return state.calls;
}

template<class T>
void expect_no_assignment_template() {
    if constexpr (std::is_same_v<T, assignment_template_value>) {
        EXPECT_EQ(T::template_assignments, 0u);
    }
}

template<class T>
void violation_matrix() {
    using H = hive_type<T>;
    for (const bool small_blocks : {false, true}) {
        for (const auto n : sizes) {
            for (const auto shape : {topology::dense, topology::churn}) {
                for (const auto kind : invalid_kinds) {
                    SCOPED_TRACE(::testing::Message() << "n=" << n << " blocks8=" << small_blocks
                        << " shape=" << static_cast<int>(shape) << " comparator=" << static_cast<int>(kind));
                    const auto limits = small_blocks ? std::hive_limits{8, 8}
                                                      : H::block_capacity_default_limits();
                    if (kind != comparison_kind::throwing) {
                        auto state = std::make_shared<allocation_state>();
                        {
                            H hive(limits, allocator<T>(state, 141));
                            populate(hive, n, input_order::duplicates, shape);
                            const observation before(hive, *state);
                            checked_sort(hive, kind);
                            before.expect_preserved(hive, *state);
                        }
                        EXPECT_TRUE(state->clean());
                        continue;
                    }

                    std::size_t total = 0;
                    auto baseline = std::make_shared<allocation_state>();
                    {
                        H hive(limits, allocator<T>(baseline, 141));
                        populate(hive, n, input_order::duplicates, shape);
                        const observation before(hive, *baseline);
                        total = checked_sort(hive);
                        before.expect_preserved(hive, *baseline);
                        before.expect_sorted(hive);
                    }
                    EXPECT_TRUE(baseline->clean());
                    if (n >= 2) ASSERT_GT(total, 0u);
                    std::vector<std::size_t> positions{total}; // Successful sentinel, even for empty input.
                    if (total != 0) {
                        positions.insert(positions.end(), {0, total / 2, total - 1});
                        std::sort(positions.begin(), positions.end());
                        positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
                    }
                    for (const auto position : positions) {
                        SCOPED_TRACE(::testing::Message() << "throw_at=" << position << " total=" << total);
                        auto state = std::make_shared<allocation_state>();
                        {
                            H hive(limits, allocator<T>(state, 141));
                            populate(hive, n, input_order::duplicates, shape);
                            const observation before(hive, *state);
                            const auto calls = checked_sort(hive, kind, position, position < total);
                            EXPECT_EQ(calls, position < total ? position + 1 : total);
                            // Comparator failure preserves the multiset, not an order promise (S-I2).
                            before.expect_preserved(hive, *state);
                            if (position == total) before.expect_sorted(hive);
                            checked_sort(hive);
                            before.expect_preserved(hive, *state);
                            before.expect_sorted(hive);
                        }
                        EXPECT_TRUE(state->clean());
                    }
                }
            }
        }
    }
}

TEST(HiveSortR41Violation, CopyPathEightComparatorVariants) { violation_matrix<multifield_value>(); }
TEST(HiveSortR41Violation, GenericPathEightComparatorVariants) { violation_matrix<generic_value>(); }

} // namespace
