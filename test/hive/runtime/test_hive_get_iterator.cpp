#include <gtest/gtest.h>

#include <hive>

#include "hive_test_fancy_allocator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

template<class T>
int payload(const T& value) {
    if constexpr (requires { value.value; }) return value.value;
    else return value;
}

template<class H>
void expect_lookup(H& hive, typename H::iterator expected,
                   const typename H::value_type* address, std::size_t blocks) {
    using const_pointer = typename H::const_pointer;
    const_pointer pointer = std::pointer_traits<const_pointer>::pointer_to(std::as_const(*expected));
    ASSERT_EQ(std::to_address(pointer), address);
    static_assert(noexcept(hive.get_iterator(pointer)));
    auto found = hive.get_iterator(pointer);
    static_assert(std::is_same_v<decltype(found), typename H::iterator>);
    const auto work = hive.__test_lookup_work();
    ASSERT_NE(found, hive.end());
    EXPECT_EQ(found, expected);
    EXPECT_EQ(std::addressof(*found), address);
    EXPECT_EQ(payload(*found), 7); // Equal values must not substitute for identity.
    EXPECT_EQ(work.first, blocks);
    EXPECT_EQ(work.second, 1u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(address) % alignof(typename H::value_type), 0u);

    const auto& constant = std::as_const(hive);
    static_assert(noexcept(constant.get_iterator(pointer)));
    auto const_found = constant.get_iterator(pointer);
    static_assert(std::is_same_v<decltype(const_found), typename H::const_iterator>);
    const auto const_work = constant.__test_lookup_work();
    ASSERT_NE(const_found, constant.end());
    EXPECT_EQ(const_found, typename H::const_iterator(expected));
    EXPECT_EQ(std::addressof(*const_found), address);
    // No explicit reset: each overload must report only its own lookup work.
    EXPECT_EQ(const_work.first, blocks);
    EXPECT_EQ(const_work.second, 1u);
}

template<class H>
struct saved_handle {
    typename H::iterator position;
    const typename H::value_type* address;
    std::size_t block;
    bool live = true;
};

template<class H>
auto fill_blocks(H& hive, std::size_t count, std::size_t block_size) {
    std::vector<saved_handle<H>> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto position = hive.emplace(7);
        result.push_back({position, std::addressof(*position), i / block_size + 1});
    }
    return result;
}

template<class H>
void expect_saved(H& hive, const std::vector<saved_handle<H>>& handles,
                  std::size_t preceding_blocks = 0) {
    for (const auto& handle : handles) {
        if (!handle.live) continue;
        SCOPED_TRACE(handle.block);
        ASSERT_NO_FATAL_FAILURE(expect_lookup(
            hive, handle.position, handle.address, preceding_blocks + handle.block));
    }
}

template<class H>
void expect_fresh_packed(H& hive, std::size_t count, std::size_t block_size) {
    ASSERT_EQ(hive.size(), count);
    // A rebuilding reshape packs sequentially into explicitly fixed-size
    // blocks. Collect fresh handles without consulting get_iterator or its hook.
    auto current = hive.begin();
    for (std::size_t i = 0; i < count; ++i, ++current) {
        ASSERT_NE(current, hive.end());
        ASSERT_NO_FATAL_FAILURE(expect_lookup(
            hive, current, std::addressof(*current), i / block_size + 1));
    }
    EXPECT_EQ(current, hive.end());
}

template<class H>
void exercise_lookup_states(H& hive) {
    constexpr std::size_t block_size = 8;
    auto handles = fill_blocks(hive, 3 * block_size, block_size);
    ASSERT_EQ(hive.capacity(), 3 * block_size);
    ASSERT_NO_FATAL_FAILURE(expect_saved(hive, handles));

    // Sixty-four reserved blocks must not contribute to active-block counts.
    hive.reserve(67 * block_size);
    ASSERT_EQ(hive.capacity(), 67 * block_size);
    ASSERT_NO_FATAL_FAILURE(expect_saved(hive, handles));
    constexpr std::array<std::size_t, 9> holes{0, 3, 7, 8, 11, 15, 16, 19, 23};
    for (auto index : holes) {
        hive.erase(handles[index].position);
        handles[index].live = false;
    }
    ASSERT_NO_FATAL_FAILURE(expect_saved(hive, handles));
    for (std::size_t i = 0; i < holes.size(); ++i) {
        auto reused = hive.emplace(7);
        const auto address = std::addressof(*reused);
        // The three original blocks remain active. Identify the reused slot
        // using recorded addresses, not traversal or get_iterator results.
        auto found = std::find_if(handles.begin(), handles.end(),
            [&](const auto& handle) { return handle.address == address; });
        ASSERT_NE(found, handles.end());
        ASSERT_FALSE(found->live);
        found->position = reused;
        found->live = true;
        ASSERT_NO_FATAL_FAILURE(expect_lookup(hive, reused, address, found->block));
    }
    ASSERT_NO_FATAL_FAILURE(expect_saved(hive, handles));

    hive.reshape(std::hive_limits{8, 16}); // Existing blocks stay compatible.
    ASSERT_NO_FATAL_FAILURE(expect_saved(hive, handles));
    handles.clear(); // Never use old handles or pointers after relocation.
    hive.reshape(std::hive_limits{5, 5});
    ASSERT_NO_FATAL_FAILURE(expect_fresh_packed(hive, 24, 5));
    hive.reserve(69 * 5);
    ASSERT_NO_FATAL_FAILURE(expect_fresh_packed(hive, 24, 5));
}

struct final_value final {
    int value;
    explicit final_value(int input) : value(input) {}
};

struct nonstandard_value {
    int value;
    explicit nonstandard_value(int input) : value(input) {}
    virtual ~nonstandard_value() = default;
};

struct alignas(256) aligned_value {
    int value;
    explicit aligned_value(int input) : value(input) {}
};

struct no_default_value {
    int value;
    no_default_value() = delete;
    explicit no_default_value(int input) : value(input) {}
};

static_assert(std::is_final_v<final_value>);
static_assert(!std::is_standard_layout_v<nonstandard_value>);
static_assert(alignof(aligned_value) == 256);
static_assert(!std::is_default_constructible_v<no_default_value>);

template<class T>
void exercise_type() {
    std::hive<T> hive(std::hive_limits{8, 8});
    ASSERT_NO_FATAL_FAILURE(exercise_lookup_states(hive));
}

TEST(HiveGetIterator, DuplicateValuesHolesReuseReserveAndReshapeHaveExactLookupWork) {
    exercise_type<int>();
}

TEST(HiveGetIterator, FinalAndNonstandardLayoutLivePointers) {
    ASSERT_NO_FATAL_FAILURE(exercise_type<final_value>());
    ASSERT_NO_FATAL_FAILURE(exercise_type<nonstandard_value>());
}

TEST(HiveGetIterator, OveralignedAndNonDefaultConstructibleLivePointers) {
    ASSERT_NO_FATAL_FAILURE(exercise_type<aligned_value>());
    ASSERT_NO_FATAL_FAILURE(exercise_type<no_default_value>());
}

TEST(HiveGetIterator, FancyPointersAcrossHolesReuseReserveAndReshape) {
    using allocator_type = forge_hive_fancy_allocator_test::fancy_allocator<final_value>;
    using hive_type = std::hive<final_value, allocator_type>;
    static_assert(!std::is_pointer_v<hive_type::const_pointer>);
    auto state = std::make_shared<forge_hive_allocator_test::allocation_state>();
    {
        hive_type hive(std::hive_limits{8, 8}, allocator_type(state, 31));
        ASSERT_NO_FATAL_FAILURE(exercise_lookup_states(hive));
    }
    EXPECT_TRUE(state->clean());
}

TEST(HiveGetIterator, SplicedPartialBlocksUseDestinationRanksAndIgnoreReservedBlocks) {
    std::hive<int> destination(std::hive_limits{8, 8});
    std::hive<int> source(std::hive_limits{8, 8});
    auto retained = fill_blocks(destination, 12, 8);
    auto migrated = fill_blocks(source, 13, 8);
    destination.reserve(66 * 8);
    source.reserve(66 * 8);
    for (auto index : {0u, 10u}) {
        destination.erase(retained[index].position);
        retained[index].live = false;
    }
    for (auto index : {1u, 9u}) {
        source.erase(migrated[index].position);
        migrated[index].live = false;
    }
    ASSERT_NO_FATAL_FAILURE(expect_saved(destination, retained));
    ASSERT_NO_FATAL_FAILURE(expect_saved(source, migrated));
    destination.splice(source);
    ASSERT_TRUE(source.empty());
    // Two destination blocks precede two migrated blocks. The former partial
    // destination tail is now an interior block with its own empty suffix.
    ASSERT_NO_FATAL_FAILURE(expect_saved(destination, retained));
    ASSERT_NO_FATAL_FAILURE(expect_saved(destination, migrated, 2));
    destination.reserve(100 * 8);
    destination.reshape(std::hive_limits{8, 16});
    ASSERT_NO_FATAL_FAILURE(expect_saved(destination, retained));
    ASSERT_NO_FATAL_FAILURE(expect_saved(destination, migrated, 2));
    retained.clear();
    migrated.clear();
    destination.reshape(std::hive_limits{4, 4});
    ASSERT_NO_FATAL_FAILURE(expect_fresh_packed(destination, 21, 4));
}

TEST(HiveGetIterator, ReactivatedBlockUsesActiveRankRatherThanAllocationOrder) {
    std::hive<int> hive(std::hive_limits{8, 8});
    auto handles = fill_blocks(hive, 24, 8);
    hive.reserve(67 * 8);
    for (std::size_t i = 8; i < 16; ++i) {
        hive.erase(handles[i].position);
        handles[i].live = false;
    }
    // The original third block is now the second active block.
    for (std::size_t i = 16; i < 24; ++i) handles[i].block = 2;
    ASSERT_NO_FATAL_FAILURE(expect_saved(hive, handles));
    for (int i = 0; i < 8; ++i) {
        auto reused = hive.emplace(7);
        const auto address = std::addressof(*reused);
        auto found = std::find_if(handles.begin() + 8, handles.begin() + 16,
            [&](const auto& handle) { return handle.address == address; });
        ASSERT_NE(found, handles.begin() + 16);
        ASSERT_FALSE(found->live);
        found->position = reused;
        found->live = true;
        found->block = 3; // Reactivation appends the previously empty block.
        ASSERT_NO_FATAL_FAILURE(expect_lookup(hive, reused, address, 3));
    }
    ASSERT_NO_FATAL_FAILURE(expect_saved(hive, handles));
    EXPECT_EQ(hive.capacity(), 67 * 8u);
}

TEST(HiveGetIterator, MaximumCapacityDenseAndSparseBlocksExamineOneCandidate) {
    constexpr std::size_t block_size = 65535;
    using hive_type = std::hive<int>;
    ASSERT_GE(hive_type::block_capacity_hard_limits().max, block_size);
    hive_type hive(std::hive_limits{block_size, block_size});
    std::array<hive_type::iterator, 6> anchors{};
    std::array<const int*, 6> addresses{};
    constexpr std::array<std::size_t, 3> offsets{0, block_size / 2, block_size - 1};
    for (std::size_t block = 0; block < 2; ++block) {
        for (std::size_t slot = 0; slot < block_size; ++slot) {
            auto inserted = hive.emplace(7);
            for (std::size_t i = 0; i < offsets.size(); ++i) {
                if (slot == offsets[i]) {
                    anchors[3 * block + i] = inserted;
                    addresses[3 * block + i] = std::addressof(*inserted);
                }
            }
        }
    }
    ASSERT_EQ(hive.capacity(), 2 * block_size);
    const auto check_anchors = [&] {
        // Alternate ranks so cumulative counters cannot accidentally pass.
        for (std::size_t block : {1u, 0u, 1u}) {
            for (std::size_t i = 0; i < offsets.size(); ++i)
                ASSERT_NO_FATAL_FAILURE(expect_lookup(
                    hive, anchors[3 * block + i], addresses[3 * block + i], block + 1));
        }
    };
    ASSERT_NO_FATAL_FAILURE(check_anchors());
    hive.reserve(6 * block_size); // Four large reserved blocks, still two active.
    ASSERT_EQ(hive.capacity(), 6 * block_size);
    ASSERT_NO_FATAL_FAILURE(check_anchors());
    for (std::size_t block = 0; block < 2; ++block) {
        hive.erase(std::next(anchors[3 * block]), anchors[3 * block + 1]);
        hive.erase(std::next(anchors[3 * block + 1]), anchors[3 * block + 2]);
    }
    ASSERT_EQ(hive.size(), 6u);
    ASSERT_NO_FATAL_FAILURE(check_anchors());
    EXPECT_EQ(hive.capacity(), 6 * block_size);
}

} // namespace
