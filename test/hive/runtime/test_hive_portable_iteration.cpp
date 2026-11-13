#include <gtest/gtest.h>

#include <hive>

#include "hive_test_allocator.hpp"

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct iteration_value {
    int id;
    std::uint64_t mark;
};

using iteration_hive = std::hive<iteration_value>;
using iterator = iteration_hive::iterator;
using const_iterator = iteration_hive::const_iterator;

// Liveness and expected marks come only from test operations, never traversal.
// Positions of new insertions are unspecified; record them only after checking
// the complete population and preservation of the old relative order.
template<class Hive = iteration_hive>
struct iteration_model {
    using mutable_iterator = typename Hive::iterator;
    using read_iterator = typename Hive::const_iterator;
    using difference_type = typename Hive::difference_type;

    struct record {
        std::uint64_t mark;
        iteration_value* address;
        mutable_iterator saved;
        read_iterator const_saved;
    };

    std::map<int, record> live;
    std::vector<int> order;
    std::uint64_t generation = 0;

    void append(Hive& hive, int& next_id, std::size_t count) {
        for (std::size_t index = 0; index < count; ++index) {
            const int id = next_id++;
            auto inserted = hive.emplace(iteration_value{id, 0});
            live.emplace(id, record{0, std::addressof(*inserted), inserted, inserted});
        }
    }

    void remove(int id) {
        live.erase(id);
        std::erase(order, id);
    }

    testing::AssertionResult erase(Hive& hive, int id) {
        const auto position = std::find(order.begin(), order.end(), id);
        if (position == order.end())
            return testing::AssertionFailure() << "test erased an unmodeled identity " << id;
        const auto next = std::next(position);
        const bool last = next == order.end();
        const int successor = last ? -1 : *next;
        auto returned = hive.erase(live.at(id).saved);
        remove(id);
        if (last ? returned != hive.end()
                 : returned == hive.end() || returned->id != successor)
            return testing::AssertionFailure() << "erase successor for identity " << id;
        return testing::AssertionSuccess();
    }

    void forget_locations() {
        // Call before a potentially rebuilding operation. Never inspect old
        // pointers or element iterators after reshape/shrink invalidates them.
        for (auto& [id, item] : live) {
            (void)id;
            item.address = nullptr;
            item.saved = mutable_iterator{};
            item.const_saved = read_iterator{};
        }
        order.clear();
    }

    testing::AssertionResult reads(const Hive& hive) const {
        auto it = hive.begin();
        for (const int id : order) {
            if (it == hive.end() || it->id != id)
                return testing::AssertionFailure() << "const forward identity " << id;
            const auto& item = live.at(id);
            if (std::addressof(*it) != item.address || it->mark != item.mark ||
                item.address->id != id || item.address->mark != item.mark)
                return testing::AssertionFailure() << "const/pointer mark for identity " << id;
            if (it != item.saved || item.saved != it || it != item.const_saved ||
                std::addressof(*item.saved) != item.address ||
                std::addressof(*item.const_saved) != item.address ||
                item.saved->mark != item.mark || item.const_saved->mark != item.mark)
                return testing::AssertionFailure() << "retained iterator for identity " << id;
            ++it;
        }
        if (it != hive.end())
            return testing::AssertionFailure() << "const forward exceeds live population";

        auto backward = hive.end();
        auto reverse = hive.rbegin();
        for (std::size_t index = order.size(); index > 0; --index) {
            if (backward == hive.begin() || reverse == hive.rend())
                return testing::AssertionFailure() << "early const reverse boundary";
            --backward; // Includes --end(), but never --begin().
            const auto& item = live.at(order[index - 1]);
            if (backward->id != order[index - 1] || reverse->id != order[index - 1] ||
                std::addressof(*backward) != item.address ||
                std::addressof(*reverse) != item.address ||
                backward->mark != item.mark || reverse->mark != item.mark)
                return testing::AssertionFailure() << "const reverse identity " << order[index - 1];
            ++reverse;
        }
        if (backward != hive.begin() || reverse != hive.rend())
            return testing::AssertionFailure() << "const reverse exceeds live population";
        return testing::AssertionSuccess();
    }

    testing::AssertionResult from_retained(Hive& hive, int id) const {
        const auto position = std::find(order.begin(), order.end(), id);
        if (position == order.end())
            return testing::AssertionFailure() << "test retained a dead identity " << id;
        const std::size_t start = static_cast<std::size_t>(position - order.begin());
        auto forward = live.at(id).saved;
        auto const_forward = live.at(id).const_saved;
        for (std::size_t index = start; index < order.size(); ++index) {
            if (forward == hive.end() || const_forward == hive.cend())
                return testing::AssertionFailure() << "early retained forward end from " << id;
            const auto& item = live.at(order[index]);
            if (forward->id != order[index] || const_forward->id != order[index] ||
                std::addressof(*forward) != item.address ||
                std::addressof(*const_forward) != item.address ||
                forward->mark != item.mark || const_forward->mark != item.mark ||
                forward != const_forward || const_forward != forward ||
                (forward <=> const_forward) != std::strong_ordering::equal)
                return testing::AssertionFailure() << "retained forward from " << id;
            auto old = forward++;
            const auto const_old = const_forward++;
            if (old != const_old || !(old < const_forward) || !(forward > const_old))
                return testing::AssertionFailure() << "mixed postfix/order from " << id;
        }
        if (forward != hive.end() || const_forward != hive.cend())
            return testing::AssertionFailure() << "retained forward exceeds population from " << id;
        // Only after bounded traversal establishes termination, use distance.
        const auto suffix = static_cast<difference_type>(order.size() - start);
        if (std::distance(live.at(id).saved, hive.end()) != suffix ||
            std::distance(live.at(id).const_saved, hive.cend()) != suffix ||
            std::next(live.at(id).saved, suffix) != hive.cend() ||
            std::prev(hive.cend(), suffix) != live.at(id).saved)
            return testing::AssertionFailure() << "next/prev/distance from " << id;

        auto backward = live.at(id).saved;
        auto const_backward = live.at(id).const_saved;
        for (std::size_t index = start + 1; index > 0; --index) {
            const auto& item = live.at(order[index - 1]);
            if (backward->id != order[index - 1] || const_backward->id != order[index - 1] ||
                std::addressof(*backward) != item.address ||
                std::addressof(*const_backward) != item.address)
                return testing::AssertionFailure() << "retained backward from " << id;
            if (index > 1) {
                if (backward == hive.begin() || const_backward == hive.cbegin())
                    return testing::AssertionFailure() << "early retained begin from " << id;
                auto old = backward--;
                auto const_old = const_backward--;
                if (old != const_old || !(backward < const_old) || !(const_backward < old))
                    return testing::AssertionFailure() << "mixed decrement/order from " << id;
            }
        }
        if (backward != hive.begin() || const_backward != hive.cbegin())
            return testing::AssertionFailure() << "retained backward misses begin from " << id;
        return testing::AssertionSuccess();
    }

    testing::AssertionResult check_and_mark(Hive& hive) {
        if (hive.size() != live.size() || hive.capacity() < live.size())
            return testing::AssertionFailure() << "size/capacity differs from insertion-id model";
        std::vector<int> observed;
        std::set<int> seen;
        auto it = hive.begin();
        for (std::size_t index = 0; index < live.size(); ++index, ++it) {
            if (it == hive.end())
                return testing::AssertionFailure() << "early mutable end at " << index;
            const int id = it->id;
            const auto found = live.find(id);
            if (found == live.end() || !seen.insert(id).second)
                return testing::AssertionFailure() << "unknown/dead/duplicate identity " << id;
            auto& item = found->second;
            if (it->mark != item.mark)
                return testing::AssertionFailure() << "unexpected mark for identity " << id;
            if (item.address == nullptr) {
                item.address = std::addressof(*it);
                item.saved = it;
                item.const_saved = it;
            } else if (std::addressof(*it) != item.address) {
                return testing::AssertionFailure() << "live identity moved " << id;
            }
            observed.push_back(id);
        }
        if (it != hive.end())
            return testing::AssertionFailure() << "mutable traversal exceeds live population";
        auto previous = order.begin();
        for (const int id : observed) {
            if (previous != order.end() && id == *previous) ++previous;
        }
        if (previous != order.end())
            return testing::AssertionFailure() << "surviving relative order changed";
        order = std::move(observed);
        auto result = reads(hive);
        if (!result) return result;
        if (order.empty()) {
            if (hive.begin() != hive.cend() || hive.cbegin() != hive.end() ||
                std::distance(hive.begin(), hive.end()) != 0)
                return testing::AssertionFailure() << "empty mixed boundaries";
            return testing::AssertionSuccess();
        }
        auto mutable_last = hive.end();
        --mutable_last;
        if (mutable_last != live.at(order.back()).const_saved ||
            std::addressof(*mutable_last) != live.at(order.back()).address)
            return testing::AssertionFailure() << "mutable --end identity/address";

        // Write from insertion-era iterators, not a newly acquired begin/end.
        // Check each write pass before another pass could mask a misplaced write.
        for (const bool reverse : {false, true}) {
            ++generation;
            for (auto& [id, item] : live)
                item.mark = (generation << 32) | static_cast<std::uint32_t>(id + 1);
            auto writable = live.at(reverse ? order.back() : order.front()).saved;
            for (std::size_t index = 0; index < order.size(); ++index) {
                const int id = reverse ? order[order.size() - 1 - index] : order[index];
                if (writable == hive.end() || writable->id != id ||
                    std::addressof(*writable) != live.at(id).address)
                    return testing::AssertionFailure() << "writable identity " << id;
                writable->mark = live.at(id).mark;
                if (!reverse) ++writable;
                else if (index + 1 < order.size()) --writable;
            }
            if (reverse ? writable != hive.begin() : writable != hive.end())
                return testing::AssertionFailure() << "writable boundary";
            result = reads(hive);
            if (!result) return result;
        }

        // Exercise old state on both sides of the eight-slot boundary and at
        // interior/terminal positions without quadratic work on large cases.
        const std::size_t count = order.size();
        for (const std::size_t index : {std::size_t{0}, std::size_t{1}, std::size_t{7},
                                        std::size_t{8}, count / 2, count - 1}) {
            if (index < count) {
                result = from_retained(hive, order[index]);
                if (!result) return result;
            }
        }
        return testing::AssertionSuccess();
    }
};

class HiveIteration : public testing::TestWithParam<bool> {
protected:
    iteration_hive make_hive() const {
        const auto minimum = iteration_hive::block_capacity_default_limits().min;
        return GetParam() ? iteration_hive(std::hive_limits{minimum, minimum}) : iteration_hive();
    }
};

TEST_P(HiveIteration, DenseBoundarySizesAndUniqueWritableMarks) {
    for (const std::size_t count : {0u, 1u, 7u, 8u, 9u, 64u, 257u, 16385u}) {
        SCOPED_TRACE(count);
        auto hive = make_hive();
        iteration_model model;
        int next_id = 0;
        model.append(hive, next_id, count);
        ASSERT_TRUE(model.check_and_mark(hive));
    }
}

TEST_P(HiveIteration, RetainedIteratorsSeeTailGrowthAndNewBlocks) {
    auto hive = make_hive();
    iteration_model model;
    int next_id = 0;
    model.append(hive, next_id, 3);
    ASSERT_TRUE(model.check_and_mark(hive));
    const int old_tail = model.order.back();
    for (const std::size_t count : {5u, 1u, 47u, 513u}) {
        SCOPED_TRACE(count);
        model.append(hive, next_id, count);
        ASSERT_TRUE(model.check_and_mark(hive));
        ASSERT_TRUE(model.from_retained(hive, old_tail));
    }
}

TEST_P(HiveIteration, ChurnPreservesInsertionEraIteratorsAndPointers) {
    auto hive = make_hive();
    iteration_model model;
    int next_id = 0;
    model.append(hive, next_id, 513);
    ASSERT_TRUE(model.check_and_mark(hive));
    const int anchor = model.order.front();
    std::mt19937_64 random(1);
    for (int round = 0; round < 3; ++round) {
        SCOPED_TRACE(round);
        const auto before = model.order;
        std::size_t removed = 0;
        for (const int id : before) {
            if (id != anchor && (random() & 1) != 0) {
                ASSERT_TRUE(model.erase(hive, id));
                ++removed;
            }
        }
        ASSERT_TRUE(model.check_and_mark(hive));
        model.append(hive, next_id, removed);
        ASSERT_TRUE(model.check_and_mark(hive));
        ASSERT_TRUE(model.from_retained(hive, anchor));
    }
}

TEST_P(HiveIteration, TailOnlyDeletionAndRefillRefreshRetainedHighWater) {
    auto hive = make_hive();
    iteration_model model;
    int next_id = 0;
    model.append(hive, next_id, 29);
    ASSERT_TRUE(model.check_and_mark(hive));
    const int surviving_tail = model.order[model.order.size() - 5];
    for (int count = 0; count < 4; ++count) {
        ASSERT_TRUE(model.erase(hive, model.order.back()));
        ASSERT_TRUE(model.check_and_mark(hive));
        ASSERT_TRUE(model.from_retained(hive, surviving_tail));
    }
    for (int count = 0; count < 12; ++count) {
        model.append(hive, next_id, 1);
        ASSERT_TRUE(model.check_and_mark(hive));
        ASSERT_TRUE(model.from_retained(hive, surviving_tail));
    }
}

TEST_P(HiveIteration, EraseWhileIteratingUsesReturnedSuccessor) {
    auto hive = make_hive();
    iteration_model model;
    int next_id = 0;
    model.append(hive, next_id, 193);
    ASSERT_TRUE(model.check_and_mark(hive));
    const auto before = model.order;
    auto it = hive.begin();
    for (const int id : before) {
        ASSERT_NE(it, hive.end());
        ASSERT_EQ(it->id, id);
        if (id % 3 != 1) {
            if (id % 2 == 0) it = hive.erase(it);
            else it = hive.erase(const_iterator(it));
            model.remove(id);
        } else {
            it->mark += 17;
            model.live.at(id).mark += 17;
            ++it;
        }
    }
    ASSERT_EQ(it, hive.end());
    ASSERT_TRUE(model.check_and_mark(hive));
    const auto survivors = model.order;
    it = hive.begin();
    for (const int id : survivors) {
        ASSERT_NE(it, hive.end());
        ASSERT_EQ(it->id, id);
        it = hive.erase(it);
        model.remove(id);
    }
    ASSERT_EQ(it, hive.end());
    ASSERT_TRUE(model.check_and_mark(hive));
    model.append(hive, next_id, 33);
    ASSERT_TRUE(model.check_and_mark(hive));
}

TEST_P(HiveIteration, RetainedTraversalMigratesThroughSpliceSwapAndMoves) {
    auto destination = make_hive();
    auto source = make_hive();
    iteration_model destination_model;
    iteration_model source_model;
    int next_id = 0;
    destination_model.append(destination, next_id, 13);
    source_model.append(source, next_id, 19);
    ASSERT_TRUE(destination_model.check_and_mark(destination));
    ASSERT_TRUE(source_model.check_and_mark(source));
    const int old_destination_tail = destination_model.order.back();
    const int old_source_head = source_model.order.front();
    const int old_source_tail = source_model.order.back();
    const auto source_order = source_model.order;
    source.reserve(source.capacity() + 64); // Exercise partial control transfer.
    destination.splice(source);
    destination_model.live.merge(source_model.live);
    source_model.order.clear();
    ASSERT_TRUE(source_model.check_and_mark(source));
    ASSERT_TRUE(destination_model.check_and_mark(destination));
    const std::set<int> source_ids(source_order.begin(), source_order.end());
    std::vector<int> migrated_order;
    for (const int id : destination_model.order) {
        if (source_ids.contains(id)) migrated_order.push_back(id);
    }
    EXPECT_EQ(migrated_order, source_order);
    for (const int id : {old_destination_tail, old_source_head, old_source_tail})
        ASSERT_TRUE(destination_model.from_retained(destination, id));

    auto other = make_hive();
    iteration_model other_model;
    other_model.append(other, next_id, 25);
    ASSERT_TRUE(other_model.check_and_mark(other));
    destination.swap(other);
    using std::swap;
    swap(destination_model, other_model);
    ASSERT_TRUE(destination_model.check_and_mark(destination));
    ASSERT_TRUE(other_model.check_and_mark(other));
    ASSERT_TRUE(other_model.from_retained(other, old_destination_tail));
    iteration_hive moved(std::move(other));
    ASSERT_TRUE(other.empty());
    ASSERT_TRUE(other_model.check_and_mark(moved));
    iteration_hive allocator_moved(std::move(moved), std::allocator<iteration_value>{});
    ASSERT_TRUE(moved.empty());
    ASSERT_TRUE(other_model.check_and_mark(allocator_moved));
    auto assigned = make_hive();
    assigned.emplace(iteration_value{-1, 0});
    assigned = std::move(allocator_moved);
    ASSERT_TRUE(allocator_moved.empty());
    ASSERT_TRUE(other_model.check_and_mark(assigned));
    other_model.append(assigned, next_id, 49);
    ASSERT_TRUE(other_model.check_and_mark(assigned));
    ASSERT_TRUE(other_model.from_retained(assigned, old_source_tail));

    assigned.trim_capacity();
    auto empty_destination = make_hive();
    empty_destination.splice(std::move(assigned)); // Whole control can migrate.
    ASSERT_TRUE(assigned.empty());
    ASSERT_TRUE(other_model.check_and_mark(empty_destination));
    ASSERT_TRUE(other_model.from_retained(empty_destination, old_destination_tail));
}

TEST_P(HiveIteration, CopyAssignmentAndConstConversionStateTransitions) {
    auto hive = make_hive();
    iteration_model model;
    int next_id = 0;
    model.append(hive, next_id, 29);
    ASSERT_TRUE(model.check_and_mark(hive));
    for (const int id : {0, 6, 7, 8, 13, 24, 27})
        ASSERT_TRUE(model.erase(hive, id));
    ASSERT_TRUE(model.check_and_mark(hive));

    // State indices: singular, nonempty end, head element, interior element.
    const std::size_t interior = model.order.size() / 2;
    const auto& head = model.live.at(model.order.front());
    const auto& middle = model.live.at(model.order[interior]);
    const std::vector<iterator> mutable_states{iterator{}, hive.end(), head.saved, middle.saved};
    const std::vector<const_iterator> const_states{
        const_iterator{}, hive.cend(), head.const_saved, middle.const_saved};

    const auto traverses = [&](auto candidate, std::size_t state) -> testing::AssertionResult {
        using iterator_type = decltype(candidate);
        if (state == 0) {
            if (candidate != iterator_type{})
                return testing::AssertionFailure() << "singular copy/assignment";
            return testing::AssertionSuccess();
        }
        const std::size_t start = state == 1 ? model.order.size() - 1
                                           : state == 2 ? 0 : interior;
        if (state == 1) {
            if (candidate != hive.cend())
                return testing::AssertionFailure() << "nonempty end copy/assignment";
            --candidate;
        }
        auto backward = candidate;
        for (std::size_t index = start; index < model.order.size(); ++index) {
            const auto& expected = model.live.at(model.order[index]);
            if (candidate == hive.cend() || candidate->id != model.order[index] ||
                std::addressof(*candidate) != expected.address || candidate->mark != expected.mark)
                return testing::AssertionFailure() << "copied forward state " << state;
            ++candidate;
        }
        if (candidate != hive.cend())
            return testing::AssertionFailure() << "copied forward boundary state " << state;
        for (std::size_t index = start + 1; index > 0; --index) {
            const auto& expected = model.live.at(model.order[index - 1]);
            if (backward == hive.cend() || backward->id != model.order[index - 1] ||
                std::addressof(*backward) != expected.address || backward->mark != expected.mark)
                return testing::AssertionFailure() << "copied backward state " << state;
            if (index > 1) {
                if (backward == hive.cbegin())
                    return testing::AssertionFailure() << "early copied begin state " << state;
                --backward;
            }
        }
        if (backward != hive.cbegin())
            return testing::AssertionFailure() << "copied backward boundary state " << state;
        return testing::AssertionSuccess();
    };

    const auto exercise_copies = [&](const auto& states) {
        using iterator_type = typename std::remove_cvref_t<decltype(states)>::value_type;
        for (std::size_t source = 0; source < states.size(); ++source) {
            SCOPED_TRACE(source);
            const iterator_type constructed(states[source]);
            ASSERT_TRUE(traverses(constructed, source));
            for (std::size_t target = 0; target < states.size(); ++target) {
                SCOPED_TRACE(target);
                iterator_type assigned(states[target]);
                assigned = states[source];
                ASSERT_TRUE(traverses(assigned, source));
                const iterator_type& alias = assigned;
                assigned = alias;
                ASSERT_TRUE(traverses(assigned, source));
            }
        }
    };
    exercise_copies(mutable_states);
    exercise_copies(const_states);
    for (std::size_t source = 0; source < mutable_states.size(); ++source) {
        SCOPED_TRACE(source);
        const const_iterator converted(mutable_states[source]);
        ASSERT_TRUE(traverses(converted, source));
        for (const auto& target : const_states) {
            const_iterator assigned(target);
            assigned = mutable_states[source];
            ASSERT_TRUE(traverses(assigned, source));
        }
    }
}

INSTANTIATE_TEST_SUITE_P(BlockLimits, HiveIteration, testing::Bool(),
    [](const testing::TestParamInfo<bool>& info) {
        return info.param ? "FixedMinimum" : "Default";
    });

} // namespace
