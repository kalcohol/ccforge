#include <gtest/gtest.h>

#include <hive>

#include "hive_test_allocator.hpp"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using forge_hive_allocator_test::allocation_state;
using forge_hive_allocator_test::stateful_allocator;

struct slot_allocation {
    const void* base;
    std::size_t count;
    std::size_t stride;
    std::size_t identity;
};

struct block_header_construction_failure {};

struct skipfield_state {
    std::shared_ptr<allocation_state> allocations = std::make_shared<allocation_state>();
    std::vector<slot_allocation> slots;
    std::vector<std::size_t> skip_requests;
    std::size_t next_identity = 0;
    std::size_t skip_max = std::numeric_limits<std::size_t>::max();
    long long skip_fail_after = -1;
    bool fail_block_construct = false;
    std::size_t block_construct_attempts = 0;
    bool oversized_request = false;
    std::size_t int_constructed = 0;
    std::size_t int_destroyed = 0;
};

// Only uint16_t rebinds have a restricted max_size or an allocation failure.
// Block-header construction has a separate, pre-construction injection point.
// Payload allocations are recognized by storage, never by the old live flag.
template<class T>
struct skipfield_allocator {
    using value_type = T;
    std::shared_ptr<skipfield_state> state;

    skipfield_allocator() : state(std::make_shared<skipfield_state>()) {}
    explicit skipfield_allocator(std::shared_ptr<skipfield_state> input) : state(input) {}
    template<class U>
    skipfield_allocator(const skipfield_allocator<U>& other) noexcept : state(other.state) {}

    std::size_t max_size() const noexcept {
        const auto ordinary = std::numeric_limits<std::size_t>::max() / sizeof(T);
        if constexpr (std::is_same_v<T, std::uint16_t>)
            return std::min(ordinary, state->skip_max);
        return ordinary;
    }

    T* allocate(std::size_t count) {
        if (count > max_size()) {
            state->oversized_request = true;
            throw std::bad_array_new_length();
        }
        if constexpr (std::is_same_v<T, std::uint16_t>) {
            state->skip_requests.push_back(count);
            if (state->skip_fail_after == 0) throw std::bad_alloc();
            if (state->skip_fail_after > 0) --state->skip_fail_after;
        }
        stateful_allocator<T> underlying(state->allocations, 23);
        T* result = underlying.allocate(count);
        if constexpr (requires(T& slot) { slot.storage; }) {
            try {
                state->slots.push_back({result, count, sizeof(T), state->next_identity++});
            } catch (...) {
                underlying.deallocate(result, count);
                throw;
            }
        }
        return result;
    }

    void deallocate(T* pointer, std::size_t count) noexcept {
        if constexpr (requires(T& slot) { slot.storage; }) {
            std::erase_if(state->slots, [&](const auto& slot) { return slot.base == pointer; });
        }
        stateful_allocator<T>(state->allocations, 23).deallocate(pointer, count);
    }

    template<class U, class... Args>
    void construct(U* pointer, Args&&... args) {
        if constexpr (requires(U& block) { block.control; block.slots; block.skip; }) {
            ++state->block_construct_attempts;
            if (state->fail_block_construct) throw block_header_construction_failure{};
        }
        std::construct_at(pointer, std::forward<Args>(args)...);
        if constexpr (std::is_same_v<U, int>) ++state->int_constructed;
    }

    template<class U>
    void destroy(U* pointer) noexcept {
        if constexpr (std::is_same_v<U, int>) ++state->int_destroyed;
        std::destroy_at(pointer);
    }

    friend bool operator==(const skipfield_allocator&, const skipfield_allocator&) = default;
};

template<class T>
int payload(const T& item) {
    if constexpr (requires { item.value; }) return item.value;
    else return static_cast<int>(item);
}

template<class T>
using test_hive = std::hive<T, skipfield_allocator<T>>;

// The model never learns expected order from hive iteration or private links.
// It records valid insertion handles, block activation/deactivation, and the
// addresses within each payload allocation. Cross-block address order is NOT
// the oracle: reactivated blocks and spliced blocks follow activation order.
template<class T>
struct live_model {
    using hive_type = test_hive<T>;
    using iterator = typename hive_type::iterator;
    struct handle {
        iterator position;
        const T* address;
        int value;
        std::size_t block;
    };

    hive_type& hive;
    std::shared_ptr<skipfield_state> state;
    std::vector<handle> handles;
    std::vector<std::size_t> block_order;

    live_model(hive_type& input, std::shared_ptr<skipfield_state> owner)
        : hive(input), state(std::move(owner)) {}

    void emplace(int value) {
        auto position = hive.emplace(value);
        const T* address = std::addressof(*position);
        const auto location = reinterpret_cast<std::uintptr_t>(address);
        const auto found = std::find_if(state->slots.begin(), state->slots.end(),
            [&](const auto& allocation) {
                const auto base = reinterpret_cast<std::uintptr_t>(allocation.base);
                return location >= base && location - base < allocation.count * allocation.stride;
            });
        ASSERT_NE(found, state->slots.end());
        if (std::find(block_order.begin(), block_order.end(), found->identity) == block_order.end())
            block_order.push_back(found->identity);
        handles.push_back({position, address, value, found->identity});
    }

    std::vector<handle> ordered() const {
        auto result = handles;
        std::sort(result.begin(), result.end(), [&](const auto& left, const auto& right) {
            const auto l = std::find(block_order.begin(), block_order.end(), left.block);
            const auto r = std::find(block_order.begin(), block_order.end(), right.block);
            if (l != r) return l < r;
            return std::less<const T*>{}(left.address, right.address);
        });
        return result;
    }

    void remove_empty_blocks() {
        std::erase_if(block_order, [&](auto block) {
            return std::none_of(handles.begin(), handles.end(),
                [&](const auto& item) { return item.block == block; });
        });
    }

    void erase(std::size_t index) {
        ASSERT_LT(index, handles.size());
        const auto victim = handles[index];
        const auto expected = ordered();
        const auto found = std::find_if(expected.begin(), expected.end(),
            [&](const auto& item) { return item.address == victim.address; });
        ASSERT_NE(found, expected.end());
        const auto next = std::next(found);
        auto successor = hive.erase(victim.position);
        EXPECT_EQ(successor, next == expected.end() ? hive.end() : next->position);
        handles.erase(handles.begin() + index);
        remove_empty_blocks();
    }

    void erase_value(int value) {
        const auto found = std::find_if(handles.begin(), handles.end(),
            [&](const auto& item) { return item.value == value; });
        ASSERT_NE(found, handles.end());
        erase(static_cast<std::size_t>(found - handles.begin()));
    }

    void erase_prefix(std::size_t count) {
        ASSERT_GT(count, 0u);
        ASSERT_LE(count, handles.size());
        const auto expected = ordered();
        const auto last = count == expected.size() ? hive.end() : expected[count].position;
        const auto successor = hive.erase(expected.front().position, last);
        EXPECT_EQ(successor, count == expected.size() ? hive.end() : expected[count].position);
        std::set<const T*, std::less<const T*>> erased;
        for (std::size_t i = 0; i < count; ++i) erased.insert(expected[i].address);
        std::erase_if(handles, [&](const auto& item) { return erased.contains(item.address); });
        remove_empty_blocks();
    }

    void splice(live_model& source) {
        hive.splice(source.hive);
        handles.insert(handles.end(), source.handles.begin(), source.handles.end());
        block_order.insert(block_order.end(), source.block_order.begin(), source.block_order.end());
        source.handles.clear();
        source.block_order.clear();
    }

    testing::AssertionResult valid() const {
        if (hive.size() != handles.size() || hive.capacity() < handles.size() ||
            hive.empty() != handles.empty())
            return testing::AssertionFailure() << "population/capacity mismatch";
        const auto expected = ordered();
        std::set<const T*, std::less<const T*>> unique;
        for (const auto& item : expected) {
            if (!unique.insert(item.address).second)
                return testing::AssertionFailure() << "duplicate live address";
        }
        auto current = hive.begin();
        const auto& constant = std::as_const(hive);
        for (std::size_t i = 0; i < expected.size(); ++i, ++current) {
            if (current == hive.end())
                return testing::AssertionFailure() << "early forward end at " << i;
            const auto& item = expected[i];
            if (current != item.position || std::addressof(*current) != item.address ||
                std::addressof(*item.position) != item.address ||
                payload(*current) != item.value || payload(*item.position) != item.value)
                return testing::AssertionFailure() << "address/identity/value/order mismatch at " << i;
            if (reinterpret_cast<std::uintptr_t>(item.address) % alignof(T) != 0)
                return testing::AssertionFailure() << "element alignment mismatch";
            if (hive.get_iterator(item.address) != item.position ||
                constant.get_iterator(item.address) != typename hive_type::const_iterator(item.position))
                return testing::AssertionFailure() << "live get_iterator mismatch at " << i;
            if (!(current < hive.end()))
                return testing::AssertionFailure() << "element does not precede end";
            if (i != 0) {
                const typename hive_type::const_iterator previous = expected[i - 1].position;
                if (!(previous < current) || (current <=> previous) != std::strong_ordering::greater)
                    return testing::AssertionFailure() << "iterator comparison disagrees with oracle";
            }
        }
        if (current != hive.end())
            return testing::AssertionFailure() << "forward traversal exceeds population";
        auto reverse = constant.rbegin();
        for (std::size_t i = expected.size(); i > 0; --i, ++reverse) {
            if (reverse == constant.rend() || std::addressof(*reverse) != expected[i - 1].address)
                return testing::AssertionFailure() << "reverse traversal disagrees with oracle";
        }
        if (reverse != constant.rend())
            return testing::AssertionFailure() << "reverse traversal exceeds population";
        // The hook supplements, rather than supplies, the external oracle.
        if (!hive.__test_invariants())
            return testing::AssertionFailure() << "I1-I8 invariant hook failed";
        return testing::AssertionSuccess();
    }
};

auto outstanding(const skipfield_state& state) {
    std::set<const void*, std::less<const void*>> result;
    for (const auto& [pointer, record] : state.allocations->allocations) {
        (void)record;
        result.insert(pointer);
    }
    return result;
}

TEST(HiveSkipfield, FourEraseCasesAtHeadInteriorAndBeforeTail) {
    // Isolated deletion; left-only extension; right-only extension; merge.
    // Prefix and near-tail variants also exercise length-one neighbors.
    const std::vector<std::vector<int>> cases{
        {0}, {3}, {6}, {0, 1}, {2, 3}, {5, 6},
        {1, 0}, {4, 3}, {6, 5}, {0, 2, 1}, {2, 4, 3}, {4, 6, 5}};
    for (std::size_t scenario = 0; scenario < cases.size(); ++scenario) {
        SCOPED_TRACE(scenario);
        auto state = std::make_shared<skipfield_state>();
        {
            test_hive<int> hive(std::hive_limits{8, 8}, skipfield_allocator<int>(state));
            live_model model(hive, state);
            for (int value = 0; value < 8; ++value) model.emplace(value);
            ASSERT_TRUE(model.valid());
            for (int value : cases[scenario]) {
                model.erase_value(value);
                ASSERT_TRUE(model.valid());
            }
            for (std::size_t i = 0; i < cases[scenario].size(); ++i) {
                model.emplace(100 + static_cast<int>(i));
                ASSERT_TRUE(model.valid());
                EXPECT_EQ(hive.capacity(), 8u);
            }
        }
        EXPECT_TRUE(state->allocations->clean());
    }
}

TEST(HiveSkipfield, FreeRunNodeMigrationPreservesEveryOtherRun) {
    auto state = std::make_shared<skipfield_state>();
    {
        test_hive<int> hive(std::hive_limits{24, 24}, skipfield_allocator<int>(state));
        live_model model(hive, state);
        for (int value = 0; value < 24; ++value) model.emplace(value);
        // Multiple nodes, a prefix, a singleton, and joins on both sides.
        const std::vector<int> holes{0, 1, 2, 6, 7, 8, 9, 12, 14, 16, 18, 17, 15};
        for (int value : holes) {
            model.erase_value(value);
            ASSERT_TRUE(model.valid());
        }
        const auto attempts = state->allocations->allocation_attempts;
        for (std::size_t i = 0; i < holes.size(); ++i) {
            model.emplace(100 + static_cast<int>(i));
            ASSERT_TRUE(model.valid());
        }
        EXPECT_EQ(hive.capacity(), 24u);
        EXPECT_EQ(state->allocations->allocation_attempts, attempts);
    }
    EXPECT_TRUE(state->allocations->clean());
}

TEST(HiveSkipfield, TailDeletionWithAndWithoutLeftRunRewindsHighWater) {
    for (bool left_run : {false, true}) {
        SCOPED_TRACE(left_run);
        auto state = std::make_shared<skipfield_state>();
        {
            test_hive<int> hive(std::hive_limits{8, 8}, skipfield_allocator<int>(state));
            live_model model(hive, state);
            for (int value = 0; value < 8; ++value) model.emplace(value);
            const auto original = model.ordered();
            if (left_run) {
                model.erase_value(5);
                ASSERT_TRUE(model.valid());
                model.erase_value(6);
                ASSERT_TRUE(model.valid());
            }
            model.erase_value(7);
            ASSERT_TRUE(model.valid());
            const int first = left_run ? 5 : 7;
            for (int index = first; index < 8; ++index) {
                model.emplace(100 + index);
                EXPECT_EQ(model.handles.back().address, original[index].address);
                ASSERT_TRUE(model.valid());
            }
            EXPECT_EQ(hive.capacity(), 8u);
        }
        EXPECT_TRUE(state->allocations->clean());
    }
}

TEST(HiveSkipfield, EmptyMiddleBlockReactivatesAfterRemainingActiveBlocks) {
    auto state = std::make_shared<skipfield_state>();
    {
        test_hive<int> hive(std::hive_limits{4, 4}, skipfield_allocator<int>(state));
        live_model model(hive, state);
        for (int value = 0; value < 12; ++value) model.emplace(value);
        for (int value : {4, 5, 6, 7}) {
            model.erase_value(value);
            ASSERT_TRUE(model.valid());
        }
        const auto attempts = state->allocations->allocation_attempts;
        for (int value = 20; value < 24; ++value) {
            model.emplace(value);
            ASSERT_TRUE(model.valid());
        }
        EXPECT_EQ(hive.capacity(), 12u);
        EXPECT_EQ(state->allocations->allocation_attempts, attempts);
        model.erase_prefix(model.handles.size());
        ASSERT_TRUE(model.valid());
        model.emplace(99);
        ASSERT_TRUE(model.valid());
    }
    EXPECT_TRUE(state->allocations->clean());
}

struct construction_failure {};

struct throwing_value {
    static inline bool fail = false;
    static inline std::size_t live = 0;
    static inline std::size_t constructed = 0;
    static inline std::size_t destroyed = 0;
    static inline const void* attempted_address = nullptr;
    int value;

    explicit throwing_value(int input) : value(input) {
        // Write the complete four-byte payload before throwing, overwriting
        // any free-run links formerly stored here.
        attempted_address = this;
        if (fail) {
            // Keep the destructive write observable even in optimized builds.
            volatile int* storage = std::addressof(value);
            *storage = input;
            throw construction_failure{};
        }
        ++live;
        ++constructed;
    }
    throwing_value(const throwing_value& other) : throwing_value(other.value) {}
    throwing_value(throwing_value&& other) : throwing_value(other.value) {}
    throwing_value& operator=(const throwing_value&) = default;
    throwing_value& operator=(throwing_value&&) = default;
    ~throwing_value() { --live; ++destroyed; }
};

static_assert(sizeof(throwing_value) == sizeof(int));

TEST(HiveSkipfield, FailedConstructionRestoresSingletonPrefixAndMigratedRunNodes) {
    struct scenario {
        std::vector<int> holes;
        bool consume_first;
    };
    const std::vector<scenario> cases{
        {{0}, false}, {{0, 1, 2}, false}, {{3}, false}, {{3, 4, 5}, false},
        {{9, 10, 11}, false}, {{0, 1, 2}, true}, {{3, 4, 5}, true}};
    for (const auto& [holes, consume_first] : cases) {
        SCOPED_TRACE(holes.front());
        SCOPED_TRACE(holes.size());
        SCOPED_TRACE(consume_first);
        throwing_value::fail = false;
        throwing_value::live = throwing_value::constructed = throwing_value::destroyed = 0;
        auto state = std::make_shared<skipfield_state>();
        {
            test_hive<throwing_value> hive(
                std::hive_limits{12, 12}, skipfield_allocator<throwing_value>(state));
            live_model model(hive, state);
            for (int value = 0; value < 12; ++value) model.emplace(value);
            const auto original = model.ordered();
            // A second free-list node must survive restoration of the head.
            model.erase_value(7);
            for (int value : holes) model.erase_value(value);
            ASSERT_TRUE(model.valid());
            std::size_t head = holes.back() == 11 ? 7 : static_cast<std::size_t>(holes.front());
            if (consume_first) {
                model.emplace(100);
                EXPECT_EQ(model.handles.back().address, original[head].address);
                ++head; // Successful reuse migrates the free-run head to this slot.
                ASSERT_TRUE(model.valid());
            }
            const auto allocations = outstanding(*state);
            const auto capacity = hive.capacity();
            const auto end = hive.end();
            const auto live = throwing_value::live;
            const auto constructed = throwing_value::constructed;
            const auto destroyed = throwing_value::destroyed;
            for (int attempt = 0; attempt < 2; ++attempt) {
                throwing_value::fail = true;
                EXPECT_THROW(hive.emplace(101), construction_failure);
                throwing_value::fail = false;
                EXPECT_EQ(throwing_value::attempted_address, original[head].address);
                ASSERT_TRUE(model.valid());
                EXPECT_EQ(hive.capacity(), capacity);
                EXPECT_EQ(hive.end(), end);
                EXPECT_EQ(outstanding(*state), allocations);
                EXPECT_EQ(throwing_value::live, live);
                EXPECT_EQ(throwing_value::constructed, constructed);
                EXPECT_EQ(throwing_value::destroyed, destroyed);
            }
            while (model.handles.size() < 12) {
                model.emplace(101);
                ASSERT_TRUE(model.valid());
                EXPECT_EQ(throwing_value::live, hive.size());
            }
            EXPECT_EQ(hive.capacity(), capacity);
        }
        EXPECT_EQ(throwing_value::live, 0u);
        EXPECT_EQ(throwing_value::constructed, throwing_value::destroyed);
        EXPECT_TRUE(state->allocations->clean());
    }
}

TEST(HiveSkipfield, SplicedInteriorBlocksKeepTheirIndependentEmptyTails) {
    auto state = std::make_shared<skipfield_state>();
    {
        test_hive<int> destination(std::hive_limits{8, 8}, skipfield_allocator<int>(state));
        test_hive<int> source(std::hive_limits{8, 8}, skipfield_allocator<int>(state));
        test_hive<int> last(std::hive_limits{8, 8}, skipfield_allocator<int>(state));
        live_model model(destination, state), incoming(source, state), final(last, state);
        for (int value = 0; value < 4; ++value) model.emplace(value);
        for (int value = 20; value < 25; ++value) incoming.emplace(value);
        for (int value = 40; value < 42; ++value) final.emplace(value);
        model.erase_value(0);
        incoming.erase_value(21);
        model.splice(incoming);
        model.splice(final);
        ASSERT_TRUE(model.valid());
        ASSERT_TRUE(incoming.valid());
        ASSERT_TRUE(final.valid());
        EXPECT_EQ(destination.capacity(), 24u);
        model.erase_value(24); // Tail of an interior block, successor is in last.
        ASSERT_TRUE(model.valid());
        for (int value = 60; model.handles.size() < 24; ++value) {
            model.emplace(value);
            ASSERT_TRUE(model.valid());
        }
        EXPECT_EQ(destination.capacity(), 24u);
    }
    EXPECT_TRUE(state->allocations->clean());
}

TEST(HiveSkipfield, LongestFreeRunDoesNotConfuseLengthWithSentinel) {
    constexpr std::size_t capacity = 65535;
    auto state = std::make_shared<skipfield_state>();
    {
        test_hive<int> hive(std::hive_limits{capacity, capacity}, skipfield_allocator<int>(state));
        live_model model(hive, state);
        for (std::size_t i = 0; i < capacity; ++i) model.emplace(static_cast<int>(i));
        ASSERT_TRUE(model.valid());
        const auto original = model.ordered();
        model.erase_prefix(capacity - 1); // Prefix length 65534, tail still live.
        ASSERT_TRUE(model.valid());
        for (int value = 0; value < 3; ++value) {
            model.emplace(100000 + value);
            EXPECT_EQ(model.handles.back().address, original[value].address);
            ASSERT_TRUE(model.valid());
        }
        model.erase_value(65534); // Collapse the huge left run into the empty tail.
        ASSERT_TRUE(model.valid());
        model.emplace(100003);
        EXPECT_EQ(model.handles.back().address, original[3].address);
        ASSERT_TRUE(model.valid());
        EXPECT_EQ(hive.capacity(), capacity);
    }
    EXPECT_TRUE(state->allocations->clean());
}

struct alignas(64) over_aligned_value {
    int value;
    explicit over_aligned_value(int input) : value(input) {}
};

template<class T>
void small_or_aligned_reuse() {
    auto state = std::make_shared<skipfield_state>();
    {
        test_hive<T> hive(std::hive_limits{16, 16}, skipfield_allocator<T>(state));
        live_model model(hive, state);
        for (int value = 0; value < 16; ++value) model.emplace(value);
        for (int value : {0, 1, 5, 7, 6, 14, 15}) {
            model.erase_value(value);
            ASSERT_TRUE(model.valid());
        }
        for (int value = 32; value < 39; ++value) {
            model.emplace(value);
            ASSERT_TRUE(model.valid());
        }
        EXPECT_EQ(hive.capacity(), 16u);
    }
    EXPECT_TRUE(state->allocations->clean());
}

TEST(HiveSkipfield, CharPayloadCanHoldUnalignedFourByteFreeRunLinks) {
    small_or_aligned_reuse<char>();
}

TEST(HiveSkipfield, FourBytePayloadAndOveralignedPayloadReuse) {
    static_assert(sizeof(std::uint32_t) == 4);
    small_or_aligned_reuse<std::uint32_t>();
    small_or_aligned_reuse<over_aligned_value>();
}

TEST(HiveSkipfield, SeededChurnChecksEveryLiveHandleAndAddressOrderAfterEachStep) {
    for (unsigned seed = 0; seed < 8; ++seed) {
        SCOPED_TRACE(seed);
        std::mt19937 rng(seed);
        auto state = std::make_shared<skipfield_state>();
        {
            test_hive<int> hive(std::hive_limits{7, 7}, skipfield_allocator<int>(state));
            live_model model(hive, state);
            int next_value = 0;
            for (int i = 0; i < 40; ++i) model.emplace(next_value++);
            for (int step = 0; step < 250; ++step) {
                SCOPED_TRACE(step);
                if (!model.handles.empty() && (rng() & 1))
                    model.erase(static_cast<std::size_t>(rng()) % model.handles.size());
                else model.emplace(next_value++);
                ASSERT_TRUE(model.valid());
            }
        }
        EXPECT_TRUE(state->allocations->clean());
    }
}

// Relocation invalidates handles. Retain an independent payload multiset oracle,
// and bound traversal before collecting new addresses for reverse checks.
void expect_relocated_values(const test_hive<int>& hive, std::vector<int> expected) {
    ASSERT_EQ(hive.size(), expected.size());
    std::vector<int> actual;
    std::vector<const int*> addresses;
    auto current = hive.begin();
    for (std::size_t i = 0; i < expected.size(); ++i, ++current) {
        ASSERT_NE(current, hive.end());
        actual.push_back(*current);
        addresses.push_back(std::addressof(*current));
        EXPECT_EQ(hive.get_iterator(addresses.back()), current);
    }
    EXPECT_EQ(current, hive.end());
    std::sort(actual.begin(), actual.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(actual, expected);
    auto reverse = hive.rbegin();
    for (std::size_t i = addresses.size(); i > 0; --i, ++reverse) {
        ASSERT_NE(reverse, hive.rend());
        EXPECT_EQ(std::addressof(*reverse), addresses[i - 1]);
    }
    EXPECT_EQ(reverse, hive.rend());
    EXPECT_TRUE(hive.__test_invariants());
}

TEST(HiveSkipfieldAllocator, SkipRebindMaxBoundsInsertionReserveAndShrink) {
    auto state = std::make_shared<skipfield_state>();
    state->skip_max = 3;
    {
        test_hive<int> hive(std::hive_limits{2, 10}, skipfield_allocator<int>(state));
        live_model model(hive, state);
        for (int value = 0; value < 11; ++value) {
            model.emplace(value);
            ASSERT_TRUE(model.valid());
        }
        EXPECT_EQ(hive.capacity(), 13u); // 2 + 2 + 3 + 3 + 3, same growth policy.
        test_hive<int> planned(std::hive_limits{2, 10}, skipfield_allocator<int>(state));
        ASSERT_NO_THROW(planned.reserve(11));
        EXPECT_EQ(planned.capacity(), 11u); // 3 + 3 + 3 + 2, same reserve policy.
        EXPECT_TRUE(planned.__test_invariants());
        ASSERT_NO_THROW(hive.shrink_to_fit());
        EXPECT_EQ(hive.capacity(), 11u);
        expect_relocated_values(hive, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
        ASSERT_FALSE(state->skip_requests.empty());
        for (auto count : state->skip_requests) EXPECT_LE(count, 3u);
        for (const auto& allocation : state->slots) EXPECT_LE(allocation.count, 3u);
        EXPECT_FALSE(state->oversized_request);
    }
    EXPECT_TRUE(state->allocations->clean());

    state->skip_max = 1;
    const auto attempts = state->allocations->allocation_attempts;
    {
        test_hive<int> hive(std::hive_limits{2, 10}, skipfield_allocator<int>(state));
        EXPECT_THROW(hive.emplace(0), std::length_error);
        EXPECT_THROW(hive.reserve(1), std::length_error);
        EXPECT_EQ(state->allocations->allocation_attempts, attempts);
        EXPECT_TRUE(hive.__test_invariants());
        EXPECT_FALSE(state->oversized_request);
    }
    EXPECT_TRUE(state->allocations->clean());
}

TEST(HiveSkipfieldAllocator, SkipAllocationFailureRollsBackInsertionAndReserve) {
    for (bool reserve : {false, true}) {
        SCOPED_TRACE(reserve);
        auto state = std::make_shared<skipfield_state>();
        {
            test_hive<int> hive(std::hive_limits{4, 4}, skipfield_allocator<int>(state));
            live_model model(hive, state);
            for (int value = 0; value < 4; ++value) model.emplace(value);
            const auto allocations = outstanding(*state);
            const auto end = hive.end();
            const auto requests = state->skip_requests.size();
            state->skip_fail_after = 0;
            if (reserve) EXPECT_THROW(hive.reserve(8), std::bad_alloc);
            else EXPECT_THROW(hive.emplace(4), std::bad_alloc);
            state->skip_fail_after = -1;
            EXPECT_EQ(state->skip_requests.size(), requests + 1);
            EXPECT_EQ(outstanding(*state), allocations);
            EXPECT_EQ(hive.capacity(), 4u);
            EXPECT_EQ(hive.end(), end);
            ASSERT_TRUE(model.valid());
            EXPECT_TRUE(state->allocations->valid_deallocations);
            if (reserve) ASSERT_NO_THROW(hive.reserve(8));
            model.emplace(4);
            ASSERT_TRUE(model.valid());
            EXPECT_EQ(hive.capacity(), 8u);
        }
        EXPECT_TRUE(state->allocations->clean());
    }
}

TEST(HiveSkipfieldAllocator, FirstSkipAllocationFailureDoesNotPublishControlOrStorage) {
    auto state = std::make_shared<skipfield_state>();
    {
        test_hive<int> hive(std::hive_limits{4, 4}, skipfield_allocator<int>(state));
        live_model model(hive, state);
        const auto end = hive.end();
        state->skip_fail_after = 0;
        EXPECT_THROW(hive.emplace(1), std::bad_alloc);
        state->skip_fail_after = -1;
        ASSERT_EQ(state->skip_requests.size(), 1u);
        EXPECT_TRUE(state->slots.empty());
        EXPECT_TRUE(state->allocations->clean());
        EXPECT_EQ(hive.capacity(), 0u);
        EXPECT_EQ(hive.end(), end);
        ASSERT_TRUE(model.valid());
        model.emplace(1);
        ASSERT_TRUE(model.valid());
    }
    EXPECT_TRUE(state->allocations->clean());
}

TEST(HiveSkipfieldAllocator, BlockHeaderConstructionFailureReleasesAllThreeAllocations) {
    for (bool populated : {false, true}) {
        for (bool reserve : {false, true}) {
            SCOPED_TRACE(populated);
            SCOPED_TRACE(reserve);
            auto state = std::make_shared<skipfield_state>();
            {
                test_hive<int> hive(std::hive_limits{4, 4}, skipfield_allocator<int>(state));
                live_model model(hive, state);
                if (populated) {
                    for (int value = 0; value < 4; ++value) model.emplace(value);
                }
                ASSERT_TRUE(model.valid());
                const auto allocations = outstanding(*state);
                const auto capacity = hive.capacity();
                const auto end = hive.end();
                const auto slots = state->slots.size();
                const auto skips = state->allocations->current_allocations_for<std::uint16_t>();
                const auto constructed = state->int_constructed;
                const auto destroyed = state->int_destroyed;
                for (int attempt = 0; attempt < 2; ++attempt) {
                    SCOPED_TRACE(attempt);
                    const auto requests = state->skip_requests.size();
                    const auto attempts = state->allocations->allocation_attempts;
                    const auto headers = state->block_construct_attempts;
                    state->fail_block_construct = true;
                    if (reserve)
                        EXPECT_THROW(hive.reserve(capacity + 4), block_header_construction_failure);
                    else
                        EXPECT_THROW(hive.emplace(42), block_header_construction_failure);
                    state->fail_block_construct = false;

                    // Failure must occur after header, payload and skipfield
                    // allocation, plus a new control for the initially empty hive.
                    EXPECT_EQ(state->allocations->allocation_attempts,
                              attempts + (populated ? 3u : 4u));
                    EXPECT_EQ(state->block_construct_attempts, headers + 1);
                    EXPECT_EQ(state->skip_requests.size(), requests + 1);
                    EXPECT_EQ(outstanding(*state), allocations);
                    EXPECT_EQ(state->slots.size(), slots);
                    EXPECT_EQ(state->allocations->current_allocations_for<std::uint16_t>(), skips);
                    EXPECT_TRUE(state->allocations->valid_deallocations);
                    EXPECT_EQ(state->int_constructed, constructed);
                    EXPECT_EQ(state->int_destroyed, destroyed);
                    EXPECT_EQ(hive.capacity(), capacity);
                    EXPECT_EQ(hive.end(), end);
                    ASSERT_TRUE(model.valid());
                }
                if (reserve) {
                    ASSERT_NO_THROW(hive.reserve(capacity + 4));
                    EXPECT_EQ(hive.end(), end);
                    ASSERT_TRUE(model.valid());
                }
                model.emplace(42);
                ASSERT_TRUE(model.valid());
                EXPECT_EQ(hive.capacity(), capacity + 4);
            }
            EXPECT_EQ(state->int_constructed, state->int_destroyed);
            EXPECT_TRUE(state->allocations->clean());
        }
    }
}

TEST(HiveSkipfieldAllocator, SkipAllocationFailureDuringShrinkPreservesLiveHandles) {
    auto state = std::make_shared<skipfield_state>();
    {
        test_hive<int> hive(std::hive_limits{4, 4}, skipfield_allocator<int>(state));
        live_model model(hive, state);
        for (int value = 0; value < 12; ++value) model.emplace(value);
        for (int value : {1, 2, 3, 5, 6, 7, 9, 10, 11}) model.erase_value(value);
        ASSERT_TRUE(model.valid());
        const auto allocations = outstanding(*state);
        const auto end = hive.end();
        const auto requests = state->skip_requests.size();
        state->skip_fail_after = 0;
        EXPECT_THROW(hive.shrink_to_fit(), std::bad_alloc);
        state->skip_fail_after = -1;
        EXPECT_EQ(state->skip_requests.size(), requests + 1);
        EXPECT_EQ(outstanding(*state), allocations);
        EXPECT_EQ(hive.capacity(), 12u);
        EXPECT_EQ(hive.end(), end);
        ASSERT_TRUE(model.valid());
        ASSERT_NO_THROW(hive.shrink_to_fit());
        EXPECT_EQ(hive.capacity(), 4u);
        expect_relocated_values(hive, {0, 4, 8});
    }
    EXPECT_TRUE(state->allocations->clean());
}

TEST(HiveSkipfieldAllocator, TrivialPayloadStillUsesDestroyHooksForEraseClearAndDestruction) {
    static_assert(std::is_trivially_destructible_v<int>);
    auto state = std::make_shared<skipfield_state>();
    {
        test_hive<int> hive(std::hive_limits{8, 8}, skipfield_allocator<int>(state));
        live_model model(hive, state);
        for (int value = 0; value < 21; ++value) model.emplace(value);
        for (int value : {0, 1, 5, 6, 10, 19, 20}) model.erase_value(value);
        ASSERT_TRUE(model.valid());
        EXPECT_EQ(state->int_constructed, 21u);
        EXPECT_EQ(state->int_destroyed, 7u);
        hive.clear();
        model.handles.clear();
        model.block_order.clear();
        ASSERT_TRUE(model.valid());
        EXPECT_EQ(state->int_destroyed, 21u);
        hive.clear();
        EXPECT_EQ(state->int_destroyed, 21u); // No hook for an already dead slot.
        for (int value = 0; value < 11; ++value) model.emplace(value);
        model.erase_value(3);
        ASSERT_TRUE(model.valid());
        EXPECT_EQ(state->int_constructed, 32u);
        EXPECT_EQ(state->int_destroyed, 22u);
    }
    EXPECT_EQ(state->int_destroyed, 32u);
    EXPECT_EQ(state->int_constructed, state->int_destroyed);
    EXPECT_TRUE(state->allocations->clean());
}

} // namespace
