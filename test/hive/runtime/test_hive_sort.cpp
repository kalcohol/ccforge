#include <gtest/gtest.h>

#include <hive>

#include "hive_test_allocator.hpp"
#include "hive_test_fancy_allocator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using forge_hive_allocator_test::allocation_state;
template<class T>
using raw_allocator = forge_hive_allocator_test::stateful_allocator<T>;
template<class T>
using fancy_allocator = forge_hive_fancy_allocator_test::fancy_allocator<T>;

struct lifetime_counts {
    std::size_t live = 0;
    std::size_t peak = 0;
    std::size_t born = 0;
    std::size_t destroyed = 0;

    void birth() noexcept {
        ++born;
        peak = std::max(peak, ++live);
    }
    void death() noexcept { ++destroyed; --live; }
};

struct element_hooks {
    std::size_t constructed = 0;
    std::size_t destroyed = 0;
};

template<class T, class Element, template<class> class Backing>
class hook_allocator;

struct trivial_64 {
    int key;
    int serial;
    std::array<std::uint64_t, 7> payload;

    auto snapshot() const { return std::tuple(key, serial, payload); }
};
static_assert(sizeof(trivial_64) == 64);
static_assert(std::is_trivially_copyable_v<trivial_64>);

template<bool Copyable>
struct counted_value {
    int key;
    int serial;
    lifetime_counts* counts;

    counted_value(int k, int s, lifetime_counts* c) noexcept
        : key(k), serial(s), counts(c) { counts->birth(); }
    counted_value(const counted_value& other) noexcept requires Copyable
        : key(other.key), serial(other.serial), counts(other.counts) { counts->birth(); }
    counted_value(const counted_value&) requires (!Copyable) = delete;
    counted_value(counted_value&& other) noexcept
        : key(std::exchange(other.key, -1)), serial(std::exchange(other.serial, -1)),
          counts(other.counts) { counts->birth(); }
    counted_value& operator=(const counted_value& other) noexcept requires Copyable {
        key = other.key;
        serial = other.serial;
        return *this;
    }
    counted_value& operator=(const counted_value&) requires (!Copyable) = delete;
    counted_value& operator=(counted_value&& other) noexcept {
        key = std::exchange(other.key, -1);
        serial = std::exchange(other.serial, -1);
        return *this;
    }
    ~counted_value() { counts->death(); }
    auto snapshot() const { return std::pair(key, serial); }
};

using move_only_value = counted_value<false>;
using small_nontrivial_value = counted_value<true>;
static_assert(!std::is_copy_constructible_v<move_only_value>);
static_assert(std::is_copy_constructible_v<small_nontrivial_value>);
static_assert(!std::is_trivially_copyable_v<small_nontrivial_value>);

class allocator_only_value {
    template<class, class, template<class> class> friend class hook_allocator;

    allocator_only_value(allocator_only_value&& other) noexcept
        : key(std::exchange(other.key, -1)), serial(std::exchange(other.serial, -1)),
          counts(other.counts) { counts->birth(); }

public:
    int key;
    int serial;
    lifetime_counts* counts;

    allocator_only_value(int k, int s, lifetime_counts* c) noexcept
        : key(k), serial(s), counts(c) { counts->birth(); }
    allocator_only_value(const allocator_only_value&) = delete;
    allocator_only_value& operator=(const allocator_only_value&) = delete;
    allocator_only_value& operator=(allocator_only_value&& other) noexcept {
        key = std::exchange(other.key, -1);
        serial = std::exchange(other.serial, -1);
        return *this;
    }
    ~allocator_only_value() { counts->death(); }
    friend void swap(allocator_only_value& left, allocator_only_value& right) noexcept {
        using std::swap;
        swap(left.key, right.key);
        swap(left.serial, right.serial);
    }
    auto snapshot() const { return std::pair(key, serial); }
};
static_assert(!std::is_move_constructible_v<allocator_only_value>);
static_assert(std::is_move_assignable_v<allocator_only_value>);
static_assert(std::is_swappable_v<allocator_only_value>);

struct deleted_move_value {
    int key;
    int serial;

    deleted_move_value(int k, int s) noexcept : key(k), serial(s) {}
    deleted_move_value(const deleted_move_value&) = default;
    deleted_move_value& operator=(const deleted_move_value&) = default;
    deleted_move_value(deleted_move_value&&) = delete;
    deleted_move_value& operator=(deleted_move_value&&) = default;
    friend void swap(deleted_move_value& left, deleted_move_value& right) noexcept {
        using std::swap;
        swap(left.key, right.key);
        swap(left.serial, right.serial);
    }
    auto snapshot() const { return std::pair(key, serial); }
};
static_assert(std::is_trivially_copyable_v<deleted_move_value>);
static_assert(std::is_trivially_copy_constructible_v<deleted_move_value>);
static_assert(std::is_trivially_copy_assignable_v<deleted_move_value>);
static_assert(!std::is_move_constructible_v<deleted_move_value>);
static_assert(std::is_move_assignable_v<deleted_move_value>);
static_assert(std::is_swappable_v<deleted_move_value>);

// Retain the shared allocation ledger and real fancy-pointer behavior. Only
// element hooks are counted; constructing a rebound index entry is independent.
template<class T, class Element, template<class> class Backing>
class hook_allocator : public Backing<T> {
    template<class, class, template<class> class> friend class hook_allocator;

public:
    using value_type = T;
    template<class U>
    struct rebind { using other = hook_allocator<U, Element, Backing>; };

    hook_allocator() : hooks_(std::make_shared<element_hooks>()) {}
    hook_allocator(std::shared_ptr<allocation_state> state,
                   std::shared_ptr<element_hooks> hooks, int id)
        : Backing<T>(std::move(state), id), hooks_(std::move(hooks)) {}
    template<class U>
    hook_allocator(const hook_allocator<U, Element, Backing>& other) noexcept
        : Backing<T>(static_cast<const Backing<U>&>(other)), hooks_(other.hooks_) {}

    template<class U, class... Args>
    void construct(U* pointer, Args&&... args) {
        if constexpr (std::is_same_v<U, deleted_move_value> && sizeof...(Args) == 1 &&
                      (std::is_same_v<Args, deleted_move_value> && ...)) {
            // Allocator-relative rvalue insertion can copy even with deleted moves.
            ::new (static_cast<void*>(pointer)) U(static_cast<const U&>(args)...);
        } else {
            // Direct construction also has access to allocator_only_value's move.
            ::new (static_cast<void*>(pointer)) U(std::forward<Args>(args)...);
        }
        if constexpr (std::is_same_v<U, Element>) ++hooks_->constructed;
    }
    template<class U>
    void destroy(U* pointer) noexcept {
        std::destroy_at(pointer);
        if constexpr (std::is_same_v<U, Element>) ++hooks_->destroyed;
    }
    template<class U>
    bool operator==(const hook_allocator<U, Element, Backing>& other) const noexcept {
        return static_cast<const Backing<T>&>(*this) ==
                   Backing<T>(static_cast<const Backing<U>&>(other)) && hooks_ == other.hooks_;
    }

private:
    std::shared_ptr<element_hooks> hooks_;
};

enum class input_order { random, sorted, reverse, duplicates, equal, organ_pipe };
enum class topology { dense, sparse, churn };

int key_of(int value) { return value; }
std::string key_of(const std::string& value) { return value.substr(0, 8); }
template<class T>
int key_of(const T& value) { return value.key; }

std::string string_payload(int key, int serial) {
    auto prefix = std::to_string(key);
    prefix.insert(0, 8 - prefix.size(), '0');
    auto suffix = std::to_string(serial);
    suffix.insert(0, 24 - suffix.size(), 'x');
    return prefix + suffix; // 32 characters, including a distinct payload suffix.
}

std::array<std::uint64_t, 7> wide_payload(int serial) {
    std::array<std::uint64_t, 7> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = static_cast<std::uint64_t>(serial) * 0x9e3779b97f4a7c15ULL + i;
    }
    return result;
}

template<class H>
void emplace_value(H& hive, int key, int serial, lifetime_counts& counts) {
    using T = typename H::value_type;
    if constexpr (std::is_same_v<T, int>) hive.emplace(key);
    else if constexpr (std::is_same_v<T, std::string>) hive.emplace(string_payload(key, serial));
    else if constexpr (std::is_same_v<T, trivial_64>) hive.emplace(key, serial, wide_payload(serial));
    else if constexpr (std::is_same_v<T, deleted_move_value>) hive.emplace(key, serial);
    else hive.emplace(key, serial, &counts);
}

template<class T>
void set_value(T& value, int key, int serial) {
    if constexpr (std::is_same_v<T, int>) value = key;
    else if constexpr (std::is_same_v<T, std::string>) value = string_payload(key, serial);
    else {
        value.key = key;
        value.serial = serial;
        if constexpr (std::is_same_v<T, trivial_64>) value.payload = wide_payload(serial);
    }
}

template<class H>
void populate(H& hive, std::size_t n, input_order order, topology shape,
              lifetime_counts& counts) {
    const std::size_t extra = shape == topology::dense || n < 3 ? 0 : n / 2 + 1;
    int serial = 0;
    for (std::size_t i = 0; i < n + extra; ++i) emplace_value(hive, 0, serial++, counts);
    if (shape == topology::churn) {
        for (int round = 0; round < 2; ++round) {
            std::size_t removed = 0;
            std::size_t index = 0;
            for (auto it = hive.begin(); it != hive.end();) {
                if (index++ % 3 == 1) { it = hive.erase(it); ++removed; }
                else ++it;
            }
            for (std::size_t i = 0; i < removed; ++i) emplace_value(hive, 0, serial++, counts);
        }
    }
    std::size_t removed = 0;
    std::size_t index = 0;
    for (auto it = hive.begin(); it != hive.end() && removed < extra;) {
        if (index++ % 2 == 0) { it = hive.erase(it); ++removed; }
        else ++it;
    }

    // Set the final traversal sequence after forming holes, so sorted/reverse
    // cases stay genuinely sorted/reverse for every storage topology.
    std::mt19937_64 rng(1);
    index = 0;
    for (auto& value : hive) {
        int key = 0;
        switch (order) {
        case input_order::random: key = static_cast<int>(rng() % (n + 17)); break;
        case input_order::sorted: key = static_cast<int>(index); break;
        case input_order::reverse: key = static_cast<int>(n - index); break;
        case input_order::duplicates: key = static_cast<int>(rng() % 5); break;
        case input_order::equal: key = 7; break;
        // Low endpoints and a high midpoint give median-of-three a poor pivot.
        case input_order::organ_pipe: key = static_cast<int>(std::min(index, n - 1 - index)); break;
        }
        set_value(value, key, static_cast<int>(++index));
    }
    EXPECT_EQ(hive.size(), n);
}

template<class T>
auto snapshot_value(const T& value) {
    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, std::string>) return value;
    else return value.snapshot();
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
    return collect(hive, [](const auto& value) { return snapshot_value(value); });
}

template<class H>
auto keys(const H& hive) {
    return collect(hive, [](const auto& value) { return key_of(value); });
}

template<class H>
auto slots(const H& hive) {
    return collect(hive, [](const auto& value) -> const void* { return std::addressof(value); });
}

template<class H, class V>
void expect_sorted_and_complete(const H& hive, V expected_contents) {
    auto expected_keys = keys(hive);
    std::sort(expected_keys.begin(), expected_keys.end());
    EXPECT_EQ(keys(hive), expected_keys);
    auto actual = contents(hive);
    std::sort(actual.begin(), actual.end());
    std::sort(expected_contents.begin(), expected_contents.end());
    EXPECT_EQ(actual, expected_contents);
}

using allocation_ledger = decltype(allocation_state::allocations);

void expect_ledger(const allocation_state& state, const allocation_ledger& before) {
    EXPECT_TRUE(state.valid_deallocations);
    ASSERT_EQ(state.allocations.size(), before.size());
    for (const auto& [pointer, record] : before) {
        const auto found = state.allocations.find(pointer);
        ASSERT_NE(found, state.allocations.end());
        EXPECT_EQ(found->second.bytes, record.bytes);
        EXPECT_EQ(found->second.alignment, record.alignment);
        EXPECT_EQ(found->second.owner_id, record.owner_id);
        EXPECT_EQ(found->second.allocated_type, record.allocated_type);
    }
}

template<class T>
constexpr bool counted = std::is_same_v<T, move_only_value> ||
                         std::is_same_v<T, small_nontrivial_value> ||
                         std::is_same_v<T, allocator_only_value>;

template<class T>
void expect_lifetime(const lifetime_counts& counts, std::size_t n) {
    if constexpr (counted<T>) {
        EXPECT_EQ(counts.live, n);
        EXPECT_LE(counts.peak, n + 1); // A swap-local temporary is permitted.
    }
}

template<class T, template<class> class Backing = raw_allocator>
void differential_matrix() {
    using A = hook_allocator<T, T, Backing>;
    using H = std::hive<T, A>;
    for (const bool small_blocks : {false, true}) {
        for (const auto n : {0u, 1u, 2u, 7u, 65u, 4096u}) {
            for (const auto order : {input_order::random, input_order::sorted,
                                     input_order::reverse, input_order::duplicates}) {
                for (const auto shape : {topology::dense, topology::sparse, topology::churn}) {
                    SCOPED_TRACE(::testing::Message() << "n=" << n << " blocks8=" << small_blocks
                        << " order=" << static_cast<int>(order) << " shape=" << static_cast<int>(shape));
                    auto state = std::make_shared<allocation_state>();
                    auto hooks = std::make_shared<element_hooks>();
                    lifetime_counts counts;
                    {
                        H hive(small_blocks ? std::hive_limits{8, 8} : H::block_capacity_default_limits(),
                               A(state, hooks, 41));
                        populate(hive, n, order, shape, counts);
                        const auto before = contents(hive);
                        auto expected_keys = keys(hive);
                        std::sort(expected_keys.begin(), expected_keys.end());
                        const auto addresses = slots(hive);
                        const auto capacity = hive.capacity();
                        const auto ledger = state->allocations;
                        const auto old_hooks = *hooks;
                        counts.peak = counts.live;
                        hive.sort([](const T& left, const T& right) { return key_of(left) < key_of(right); });
                        EXPECT_EQ(hive.size(), n);
                        EXPECT_EQ(hive.capacity(), capacity);
                        EXPECT_EQ(slots(hive), addresses);
                        EXPECT_EQ(keys(hive), expected_keys);
                        expect_sorted_and_complete(hive, before);
                        expect_ledger(*state, ledger);
                        EXPECT_EQ(hooks->constructed, old_hooks.constructed);
                        EXPECT_EQ(hooks->destroyed, old_hooks.destroyed);
                        expect_lifetime<T>(counts, n);
                    }
                    EXPECT_TRUE(state->clean());
                    EXPECT_EQ(counts.live, 0u);
                    EXPECT_EQ(counts.born, counts.destroyed);
                    EXPECT_EQ(hooks->constructed, hooks->destroyed);
                }
            }
        }
    }
}

TEST(HiveSortRegression, IntDifferentialKeysAndPayloadMultiset) { differential_matrix<int>(); }
TEST(HiveSortRegression, Trivial64DifferentialKeysAndPayloadMultiset) { differential_matrix<trivial_64>(); }
TEST(HiveSortRegression, StringDifferentialKeysAndPayloadMultiset) { differential_matrix<std::string>(); }
TEST(HiveSortRegression, MoveOnlyDifferentialKeysAndPayloadMultiset) { differential_matrix<move_only_value>(); }
TEST(HiveSortRegression, SmallNontrivialDifferentialKeysAndPayloadMultiset) {
    differential_matrix<small_nontrivial_value>();
}
TEST(HiveSortRegression, AllocatorOnlyAdlSwapDifferentialKeysAndPayloadMultiset) {
    differential_matrix<allocator_only_value>();
}
TEST(HiveSortRegression, DeletedMoveTrivialCopyDifferentialKeysAndPayloadMultiset) {
    differential_matrix<deleted_move_value>();
}

struct comparator_failure { std::size_t position; };

template<class T>
struct counted_less {
    std::size_t* comparisons;
    std::size_t throw_at = std::numeric_limits<std::size_t>::max();

    bool operator()(const T& left, const T& right) {
        const auto position = (*comparisons)++;
        if (position == throw_at) throw comparator_failure{position};
        return key_of(left) < key_of(right);
    }
};

template<class T, template<class> class Backing = raw_allocator>
void comparator_failure_sweep() {
    using A = hook_allocator<T, T, Backing>;
    using H = std::hive<T, A>;
    constexpr std::size_t n = 37;
    for (const bool small_blocks : {false, true}) {
        for (const auto shape : {topology::dense, topology::sparse, topology::churn}) {
            std::size_t total = 0;
            {
                lifetime_counts counts;
                H hive(small_blocks ? std::hive_limits{8, 8} : H::block_capacity_default_limits());
                populate(hive, n, input_order::duplicates, shape, counts);
                counts.peak = counts.live;
                hive.sort(counted_less<T>{&total});
                expect_lifetime<T>(counts, n);
            }
            ASSERT_GT(total, 0u);
            // Rebuild for every comparison, through the successful sentinel.
            for (std::size_t throw_at = 0; throw_at <= total; ++throw_at) {
                SCOPED_TRACE(::testing::Message() << "comparison=" << throw_at
                    << " blocks8=" << small_blocks << " shape=" << static_cast<int>(shape));
                auto state = std::make_shared<allocation_state>();
                auto hooks = std::make_shared<element_hooks>();
                lifetime_counts counts;
                {
                    H hive(small_blocks ? std::hive_limits{8, 8} : H::block_capacity_default_limits(),
                           A(state, hooks, 43));
                    populate(hive, n, input_order::duplicates, shape, counts);
                    const auto before = contents(hive);
                    auto multiset = before;
                    std::sort(multiset.begin(), multiset.end());
                    const auto addresses = slots(hive);
                    const auto capacity = hive.capacity();
                    const auto ledger = state->allocations;
                    const auto old_hooks = *hooks;
                    counts.peak = counts.live;
                    std::size_t comparisons = 0;
                    bool threw = false;
                    try {
                        hive.sort(counted_less<T>{&comparisons, throw_at});
                    } catch (const comparator_failure& failure) {
                        threw = true;
                        EXPECT_EQ(failure.position, throw_at);
                    }
                    EXPECT_EQ(threw, throw_at < total);
                    EXPECT_EQ(comparisons, threw ? throw_at + 1 : total);
                    EXPECT_EQ(hive.size(), n);
                    EXPECT_EQ(hive.capacity(), capacity);
                    EXPECT_EQ(slots(hive), addresses);
                    auto actual = contents(hive);
                    std::sort(actual.begin(), actual.end());
                    // S-I2 is an observation: do not assert order after a throw.
                    EXPECT_EQ(actual, multiset);
                    expect_ledger(*state, ledger);
                    EXPECT_EQ(hooks->constructed, old_hooks.constructed);
                    EXPECT_EQ(hooks->destroyed, old_hooks.destroyed);
                    expect_lifetime<T>(counts, n);
                    hive.sort([](const T& left, const T& right) { return key_of(left) < key_of(right); });
                    expect_sorted_and_complete(hive, before);
                    expect_lifetime<T>(counts, n);
                    hive.clear();
                    emplace_value(hive, 3, 99, counts);
                    EXPECT_EQ(hive.size(), 1u);
                }
                EXPECT_TRUE(state->clean());
                EXPECT_EQ(counts.live, 0u);
                EXPECT_EQ(counts.born, counts.destroyed);
                EXPECT_EQ(hooks->constructed, hooks->destroyed);
            }
        }
    }
}

TEST(HiveSortRegression, IntExhaustsComparatorThrowsWithoutOrderPromise) {
    comparator_failure_sweep<int>();
}
TEST(HiveSortRegression, GenericExhaustsComparatorThrowsWithoutOrderPromise) {
    comparator_failure_sweep<move_only_value>();
}

template<class T, template<class> class Backing>
void allocation_failure_sweep() {
    using A = hook_allocator<T, T, Backing>;
    using H = std::hive<T, A>;
    constexpr std::size_t n = 1025;
    for (const bool small_blocks : {false, true}) {
        for (const auto shape : {topology::dense, topology::sparse, topology::churn}) {
            std::size_t allocations = 0;
            auto successful_state = std::make_shared<allocation_state>();
            {
                lifetime_counts counts;
                H hive(small_blocks ? std::hive_limits{8, 8} : H::block_capacity_default_limits(),
                       A(successful_state, std::make_shared<element_hooks>(), 47));
                populate(hive, n, input_order::random, shape, counts);
                const auto attempts = successful_state->allocation_attempts;
                hive.sort([](const T& left, const T& right) { return key_of(left) < key_of(right); });
                allocations = successful_state->allocation_attempts - attempts;
            }
            EXPECT_TRUE(successful_state->clean());
            // The large case must obtain scratch storage through allocator rebind.
            ASSERT_GT(allocations, 0u);
            for (std::size_t fail_at = 0; fail_at <= allocations; ++fail_at) {
                SCOPED_TRACE(::testing::Message() << "allocation=" << fail_at
                    << " blocks8=" << small_blocks << " shape=" << static_cast<int>(shape));
                auto state = std::make_shared<allocation_state>();
                auto hooks = std::make_shared<element_hooks>();
                lifetime_counts counts;
                {
                    H hive(small_blocks ? std::hive_limits{8, 8} : H::block_capacity_default_limits(),
                           A(state, hooks, 47));
                    populate(hive, n, input_order::random, shape, counts);
                    const auto before = contents(hive);
                    const auto addresses = slots(hive);
                    const auto capacity = hive.capacity();
                    const auto ledger = state->allocations;
                    const auto attempts = state->allocation_attempts;
                    const auto old_hooks = *hooks;
                    counts.peak = counts.live;
                    state->fail_after = static_cast<long long>(fail_at);
                    bool threw = false;
                    try {
                        hive.sort([](const T& left, const T& right) { return key_of(left) < key_of(right); });
                    } catch (const std::bad_alloc&) {
                        threw = true;
                    }
                    state->fail_after = -1;
                    EXPECT_EQ(threw, fail_at < allocations);
                    EXPECT_EQ(state->allocation_attempts - attempts, fail_at);
                    EXPECT_EQ(hive.size(), n);
                    EXPECT_EQ(hive.capacity(), capacity);
                    EXPECT_EQ(slots(hive), addresses);
                    if (threw) EXPECT_EQ(contents(hive), before); // S-I6 includes exact order.
                    else expect_sorted_and_complete(hive, before);
                    expect_ledger(*state, ledger);
                    EXPECT_EQ(hooks->constructed, old_hooks.constructed);
                    EXPECT_EQ(hooks->destroyed, old_hooks.destroyed);
                    expect_lifetime<T>(counts, n);
                    hive.sort([](const T& left, const T& right) { return key_of(left) < key_of(right); });
                    expect_sorted_and_complete(hive, before);
                    expect_lifetime<T>(counts, n);
                }
                EXPECT_TRUE(state->clean());
                EXPECT_EQ(counts.live, 0u);
                EXPECT_EQ(counts.born, counts.destroyed);
                EXPECT_EQ(hooks->constructed, hooks->destroyed);
            }
        }
    }
}

TEST(HiveSortRegression, IntExhaustsAllocationFailuresPreservingExactOrder) {
    allocation_failure_sweep<int, raw_allocator>();
}
TEST(HiveSortRegression, GenericExhaustsAllocationFailuresPreservingExactOrder) {
    allocation_failure_sweep<small_nontrivial_value, raw_allocator>();
}
TEST(HiveSortRegression, FancyIntExhaustsAllocationFailuresPreservingExactOrder) {
    allocation_failure_sweep<int, fancy_allocator>();
}
TEST(HiveSortRegression, FancyGenericExhaustsAllocationFailuresAndPreservesLifetimes) {
    allocation_failure_sweep<move_only_value, fancy_allocator>();
}
TEST(HiveSortRegression, FancyGenericExhaustsComparatorThrowsAndPreservesLifetimes) {
    comparator_failure_sweep<move_only_value, fancy_allocator>();
}

template<class T>
void comparison_budgets() {
    using H = std::hive<T>;
    for (const auto n : {257u, 4096u, 16384u}) {
        std::size_t ceiling_log = 0;
        for (std::size_t power = 1; power < n; power *= 2) ++ceiling_log;
        const std::size_t budget = 8 * static_cast<std::size_t>(n) * ceiling_log + 8 * n + 64;
        for (const bool small_blocks : {false, true}) {
            for (const auto shape : {topology::dense, topology::churn}) {
                for (const auto order : {input_order::sorted, input_order::reverse,
                                         input_order::equal, input_order::organ_pipe}) {
                    SCOPED_TRACE(::testing::Message() << "n=" << n << " blocks8=" << small_blocks
                        << " order=" << static_cast<int>(order) << " shape=" << static_cast<int>(shape));
                    lifetime_counts counts;
                    {
                        H hive(small_blocks ? std::hive_limits{8, 8} : H::block_capacity_default_limits());
                        populate(hive, n, order, shape, counts);
                        const auto before = contents(hive);
                        counts.peak = counts.live;
                        std::size_t comparisons = 0;
                        hive.sort(counted_less<T>{&comparisons});
                        EXPECT_LE(comparisons, budget);
                        expect_sorted_and_complete(hive, before);
                        expect_lifetime<T>(counts, n);
                    }
                    EXPECT_EQ(counts.live, 0u);
                    EXPECT_EQ(counts.born, counts.destroyed);
                }
            }
        }
    }
}

TEST(HiveSortRegression, IntComparisonBudgetSortedReverseEqualAndMedianThreeAdversarial) {
    comparison_budgets<int>();
}
TEST(HiveSortRegression, GenericComparisonBudgetSortedReverseEqualAndMedianThreeAdversarial) {
    comparison_budgets<small_nontrivial_value>();
}

TEST(HiveSortRegression, StatefulNonconstByValueComparatorCanSortDescending) {
    struct descending {
        std::size_t* calls;
        int direction;
        bool operator()(int left, int right) {
            ++*calls;
            return direction * left < direction * right;
        }
    };
    lifetime_counts unused;
    std::hive<int> hive(std::hive_limits{8, 8});
    populate(hive, 257, input_order::duplicates, topology::churn, unused);
    auto expected = contents(hive);
    std::sort(expected.begin(), expected.end(), [](int left, int right) { return left > right; });
    std::size_t calls = 0;
    hive.sort(descending{&calls, -1});
    EXPECT_GT(calls, 0u);
    EXPECT_EQ(contents(hive), expected);
}

TEST(HiveSortRegression, DeletedMoveTrivialCopySupportsAllocatorRvalueInsertionAndAdlSort) {
    using T = deleted_move_value;
    using A = hook_allocator<T, T, fancy_allocator>;
    auto state = std::make_shared<allocation_state>();
    auto hooks = std::make_shared<element_hooks>();
    {
        std::hive<T, A> hive(std::hive_limits{8, 8}, A(state, hooks, 53));
        T source(3, 91);
        const auto inserted = hive.insert(std::move(source));
        EXPECT_EQ(inserted->snapshot(), std::pair(3, 91));
        EXPECT_EQ(source.snapshot(), std::pair(3, 91));
        lifetime_counts unused;
        for (int i = 0; i < 67; ++i) emplace_value(hive, (67 - i) % 9, i, unused);
        const auto before = contents(hive);
        const auto old_hooks = *hooks;
        hive.sort([](const T& left, const T& right) { return left.key < right.key; });
        expect_sorted_and_complete(hive, before);
        EXPECT_EQ(hooks->constructed, old_hooks.constructed);
        EXPECT_EQ(hooks->destroyed, old_hooks.destroyed);
    }
    EXPECT_TRUE(state->clean());
    EXPECT_EQ(hooks->constructed, hooks->destroyed);
}

TEST(HiveSortRegression, PathSelectionMatchesRealScratchAllocation) {
    static_assert(std::hive<int>::__test_sort_uses_value_buffer());
    static_assert(std::hive<std::array<int, 4>>::__test_sort_uses_value_buffer());
    static_assert(!std::hive<trivial_64>::__test_sort_uses_value_buffer());
    static_assert(!std::hive<std::string>::__test_sort_uses_value_buffer());
    static_assert(!std::hive<move_only_value>::__test_sort_uses_value_buffer());
    static_assert(!std::hive<allocator_only_value>::__test_sort_uses_value_buffer());
    static_assert(!std::hive<small_nontrivial_value>::__test_sort_uses_value_buffer());
    static_assert(!std::hive<deleted_move_value>::__test_sort_uses_value_buffer());

    const auto check = []<class T>(bool copies) {
        auto state = std::make_shared<allocation_state>();
        lifetime_counts unused;
        {
            std::hive<T, raw_allocator<T>> hive(std::hive_limits{8, 8}, raw_allocator<T>(state, 57));
            populate(hive, 4096, input_order::random, topology::churn, unused);
            const auto before = contents(hive);
            const auto typed = state->template allocation_attempts_for<T>();
            const auto total = state->allocation_attempts;
            hive.sort([](const T& a, const T& b) { return key_of(a) < key_of(b); });
            EXPECT_EQ(state->template allocation_attempts_for<T>() - typed, copies ? 1u : 0u);
            EXPECT_EQ(state->allocation_attempts - total, 1u);
            expect_sorted_and_complete(hive, before);
        }
        EXPECT_TRUE(state->clean());
    };
    check.template operator()<int>(true);
    check.template operator()<trivial_64>(false);
}

struct comparator_identity {
    const void* first = nullptr;
    std::size_t calls = 0;
    std::size_t copies = 0;
    bool consistent = true;
};

template<class T>
struct identity_less {
    comparator_identity* state;
    explicit identity_less(comparator_identity* s) : state(s) {}
    identity_less(const identity_less& other) : state(other.state) { ++state->copies; }
    bool operator()(const T& a, const T& b) {
        if (state->first == nullptr) state->first = this;
        state->consistent = state->consistent && state->first == this;
        ++state->calls;
        return key_of(a) < key_of(b);
    }
};

TEST(HiveSortRegression, BothPathsUseOneMutableComparatorInstance) {
    const auto check = []<class T>() {
        lifetime_counts unused;
        std::hive<T> hive(std::hive_limits{8, 8});
        populate(hive, 1024, input_order::random, topology::churn, unused);
        const auto before = contents(hive);
        comparator_identity identity;
        hive.sort(identity_less<T>{&identity});
        EXPECT_GT(identity.calls, 0u);
        EXPECT_EQ(identity.copies, 0u);
        EXPECT_TRUE(identity.consistent);
        expect_sorted_and_complete(hive, before);
    };
    check.template operator()<int>();
    check.template operator()<trivial_64>();
}

struct swap_counted_value {
    int key;
    int serial;
    inline static std::size_t swaps = 0;

    swap_counted_value(int k, int s) : key(k), serial(s) {}
    ~swap_counted_value() {} // Keep this on the generic path.
    friend void swap(swap_counted_value& a, swap_counted_value& b) noexcept {
        ++swaps;
        using std::swap;
        swap(a.key, b.key);
        swap(a.serial, b.serial);
    }
};

TEST(HiveSortRegression, GenericPermutationUsesAtMostNMinusOneSwaps) {
    for (const auto n : {2u, 7u, 257u, 4096u}) {
        for (const bool duplicates : {false, true}) {
            std::hive<swap_counted_value> hive(std::hive_limits{8, 8});
            std::vector<std::pair<int, int>> expected;
            std::mt19937_64 rng(1);
            for (unsigned i = 0; i < n; ++i) {
                const int key = static_cast<int>(rng() % (duplicates ? 3 : n));
                hive.emplace(key, static_cast<int>(i));
                expected.emplace_back(key, static_cast<int>(i));
            }
            swap_counted_value::swaps = 0;
            hive.sort([](const auto& a, const auto& b) { return a.key < b.key; });
            EXPECT_LE(swap_counted_value::swaps, n - 1);
            EXPECT_TRUE(std::is_sorted(hive.begin(), hive.end(),
                [](const auto& a, const auto& b) { return a.key < b.key; }));
            std::vector<std::pair<int, int>> actual;
            for (const auto& value : hive) actual.emplace_back(value.key, value.serial);
            std::sort(actual.begin(), actual.end());
            std::sort(expected.begin(), expected.end());
            EXPECT_EQ(actual, expected);
        }
    }
}

} // namespace
