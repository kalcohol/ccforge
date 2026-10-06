#include <gtest/gtest.h>

#include <execution>

#include <cstddef>
#include <initializer_list>
#include <limits>

namespace {

namespace ex = std::execution;
namespace scope_detail = ex::__forge_counting_scope;
using state = scope_detail::__scope_state;

constexpr state states[]{
    state::unused, state::unused_and_closed, state::open,
    state::open_and_joining, state::closed, state::closed_and_joining,
    state::joined};

template<class Scope>
void expect_public_association_cleanup() {
    Scope scope;
    const auto token = scope.get_token();
    EXPECT_EQ(scope.count(), 0u);
    EXPECT_FALSE(scope.is_closed());
    {
        auto first = token.try_associate();
        ASSERT_TRUE(static_cast<bool>(first));
        EXPECT_EQ(scope.count(), 1u);
        auto second = first.try_associate();
        ASSERT_TRUE(static_cast<bool>(second));
        EXPECT_EQ(scope.count(), 2u);

        first = decltype(first){};
        EXPECT_EQ(scope.count(), 1u);
        auto replacement = token.try_associate();
        ASSERT_TRUE(static_cast<bool>(replacement));
        EXPECT_EQ(scope.count(), 2u);

        scope.close();
        EXPECT_TRUE(scope.is_closed());
        EXPECT_FALSE(static_cast<bool>(token.try_associate()));
        EXPECT_FALSE(static_cast<bool>(second.try_associate()));
        EXPECT_FALSE(static_cast<bool>(replacement.try_associate()));
        EXPECT_EQ(scope.count(), 2u);
    }
    EXPECT_EQ(scope.count(), 0u);
    const auto joined = ex::sync_wait(scope.join());
    EXPECT_TRUE(joined.has_value());
    EXPECT_FALSE(static_cast<bool>(token.try_associate()));
    EXPECT_EQ(scope.count(), 0u);
}

template<class Scope>
void expect_unused_closed_scope_stays_empty() {
    Scope scope;
    const auto token = scope.get_token();
    scope.close();
    EXPECT_TRUE(scope.is_closed());
    EXPECT_FALSE(static_cast<bool>(token.try_associate()));
    EXPECT_EQ(scope.count(), 0u);
    const auto joined = ex::sync_wait(scope.join());
    EXPECT_TRUE(joined.has_value());
    EXPECT_FALSE(static_cast<bool>(token.try_associate()));
    EXPECT_EQ(scope.count(), 0u);
}

} // namespace

TEST(CountingScopeLimitsTest, PublicConstantsAreAddressable) {
    const std::size_t* limits[]{
        &ex::simple_counting_scope::max_associations,
        &ex::counting_scope::max_associations};
    for (const auto* limit : limits) {
        EXPECT_EQ(*limit, std::numeric_limits<std::size_t>::max());
    }
}

TEST(CountingScopeLimitsTest, ZeroCapacityDoesNotChangeAnyState) {
    for (state current : states) {
        SCOPED_TRACE(static_cast<int>(current));
        const auto next = scope_detail::__scope_try_associate(current, 0, 0);
        EXPECT_FALSE(next.__engaged);
        EXPECT_EQ(next.__count, 0u);
        EXPECT_EQ(next.__state, current);
    }
}

TEST(CountingScopeLimitsTest, EmptyCountRespectsOpenAndClosedStates) {
    for (state current : states) {
        SCOPED_TRACE(static_cast<int>(current));
        const bool accepts = current == state::unused || current == state::open ||
                             current == state::open_and_joining;
        const auto next = scope_detail::__scope_try_associate(current, 0, 1);
        EXPECT_EQ(next.__engaged, accepts);
        EXPECT_EQ(next.__count, accepts ? 1u : 0u);
        EXPECT_EQ(next.__state,
                  current == state::unused ? state::open : current);
    }
}

TEST(CountingScopeLimitsTest, FullCapacityDoesNotChangeCountOrState) {
    const std::size_t limits[]{
        ex::simple_counting_scope::max_associations,
        ex::counting_scope::max_associations};
    for (std::size_t limit : limits) {
        for (state current : states) {
            SCOPED_TRACE(static_cast<int>(current));
            const auto next = scope_detail::__scope_try_associate(current, limit, limit);
            EXPECT_FALSE(next.__engaged);
            EXPECT_EQ(next.__count, limit);
            EXPECT_EQ(next.__state, current);
        }
    }
}

TEST(CountingScopeLimitsTest, FinalSlotDoesNotWrapAndCanBeReusedAfterRelease) {
    const std::size_t limits[]{
        ex::simple_counting_scope::max_associations,
        ex::counting_scope::max_associations};
    for (std::size_t limit : limits) {
        for (state current : {state::open, state::open_and_joining}) {
            SCOPED_TRACE(static_cast<int>(current));
            const auto last =
                scope_detail::__scope_try_associate(current, limit - 1, limit);
            ASSERT_TRUE(last.__engaged);
            EXPECT_EQ(last.__count, limit);
            EXPECT_EQ(last.__state, current);

            const auto full =
                scope_detail::__scope_try_associate(last.__state, last.__count, limit);
            EXPECT_FALSE(full.__engaged);
            EXPECT_EQ(full.__count, last.__count);
            EXPECT_EQ(full.__state, last.__state);

            const auto released = scope_detail::__scope_try_associate(
                full.__state, full.__count - 1, limit);
            EXPECT_TRUE(released.__engaged);
            EXPECT_EQ(released.__count, limit);
            EXPECT_EQ(released.__state, current);
        }
    }
}

TEST(CountingScopeLimitsTest, ClosedStateRejectsBelowCapacity) {
    constexpr state closed_states[]{
        state::unused_and_closed, state::closed, state::closed_and_joining,
        state::joined};
    const auto limit = ex::simple_counting_scope::max_associations;
    for (state current : closed_states) {
        for (std::size_t count : {std::size_t{0}, limit - 1}) {
            SCOPED_TRACE(static_cast<int>(current));
            const auto next = scope_detail::__scope_try_associate(current, count, limit);
            EXPECT_FALSE(next.__engaged);
            EXPECT_EQ(next.__count, count);
            EXPECT_EQ(next.__state, current);
        }
    }
}

TEST(CountingScopeLimitsTest, PublicTokenAndAssociationReleaseTheirCounts) {
    expect_public_association_cleanup<ex::simple_counting_scope>();
    expect_public_association_cleanup<ex::counting_scope>();
}

TEST(CountingScopeLimitsTest, PublicUnusedClosedScopesRemainEmpty) {
    expect_unused_closed_scope_stays_empty<ex::simple_counting_scope>();
    expect_unused_closed_scope_stays_empty<ex::counting_scope>();
}
