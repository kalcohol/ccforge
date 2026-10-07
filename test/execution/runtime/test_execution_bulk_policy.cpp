#include <gtest/gtest.h>

#include "../bulk_policy_probe.hpp"

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using namespace bulk_policy_probe;

template<bool Chunked, category Category, bool Pipe>
void check_owned() {
    policy_state state;
    int calls = 0;
    {
        bound_t<Chunked, copyable_policy> bound{
            copyable_policy{state}, 3, callback<Chunked>{&calls}};
        ASSERT_EQ(state.alive, 1);
        state.constructions = {};
        auto sender = apply<Category, Pipe>(bound);
        static_assert(std::same_as<stored_policy_t<decltype(sender)>, copyable_policy>);
        EXPECT_EQ(state.alive, 2);
        for (std::size_t i = 0; i != state.constructions.size(); ++i) {
            EXPECT_EQ(state.constructions[i], i == static_cast<std::size_t>(Category) ? 1 : 0);
        }
        EXPECT_NE(std::addressof(sender.__policy), std::addressof(bound.__policy_));
        bound.__policy_.value = 99;
        EXPECT_EQ(sender.__policy.value, 7);
        auto copy = sender;
        auto moved = std::move(copy);
        EXPECT_EQ(moved.__policy.value, 7);
        auto result = ex::sync_wait(std::move(moved));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 13);
        EXPECT_EQ(calls, Chunked ? 1 : 3);
    }
    EXPECT_EQ(state.alive, 0);
}

template<bool Chunked, category Category, bool Pipe, class Policy>
void check_borrowed(int shape) {
    policy_state state;
    int calls = 0;
    {
        bound_t<Chunked, Policy> bound{Policy{state}, shape, callback<Chunked>{&calls}};
        ASSERT_EQ(state.alive, 1);
        state.constructions = {};
        auto sender = apply<Category, Pipe>(bound);
        static_assert(std::same_as<stored_policy_t<decltype(sender)>, const Policy&>);
        EXPECT_EQ(std::addressof(sender.__policy), std::addressof(bound.__policy_));
        bound.__policy_.value = 99;
        EXPECT_EQ(sender.__policy.value, 99);
        auto copy = sender;
        auto moved = std::move(copy);
        EXPECT_EQ(std::addressof(copy.__policy), std::addressof(bound.__policy_));
        EXPECT_EQ(std::addressof(moved.__policy), std::addressof(bound.__policy_));
        EXPECT_EQ(state.alive, 1);
        EXPECT_EQ(state.constructions, (std::array<int, 4>{}));
        auto result = ex::sync_wait(std::move(moved));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), shape > 0 ? 13 : 10);
        EXPECT_EQ(calls, shape > 0 ? (Chunked ? 1 : 3) : 0);
        EXPECT_EQ(state.alive, 1);
        EXPECT_EQ(state.constructions, (std::array<int, 4>{}));
    }
    EXPECT_EQ(state.alive, 0);
}

template<bool Chunked, bool Pipe>
void check_owned_categories() {
    check_owned<Chunked, category::lvalue, Pipe>();
    check_owned<Chunked, category::const_lvalue, Pipe>();
    check_owned<Chunked, category::rvalue, Pipe>();
    check_owned<Chunked, category::const_rvalue, Pipe>();
}

template<bool Chunked, bool Pipe, class Policy>
void check_borrowed_categories() {
    for (int shape : {3, 0, -1}) {
        SCOPED_TRACE(shape);
        check_borrowed<Chunked, category::lvalue, Pipe, Policy>(shape);
        check_borrowed<Chunked, category::const_lvalue, Pipe, Policy>(shape);
        check_borrowed<Chunked, category::rvalue, Pipe, Policy>(shape);
        check_borrowed<Chunked, category::const_rvalue, Pipe, Policy>(shape);
    }
}

#if FORGE_TEST_BULK_HAS_NATIVE_POLICIES
template<class Algo, class Policy, class Fn>
void check_native(Algo algo, const Policy& policy, Fn fn) {
    auto sender = algo(ex::just(10), policy, 3, fn);
    using stored = stored_policy_t<decltype(sender)>;
    if constexpr (std::copy_constructible<Policy>) {
        static_assert(std::same_as<stored, Policy>);
        EXPECT_NE(std::addressof(sender.__policy), std::addressof(policy));
    } else {
        static_assert(std::same_as<stored, const Policy&>);
        EXPECT_EQ(std::addressof(sender.__policy), std::addressof(policy));
    }
    auto result = ex::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 13);
    if constexpr (std::constructible_from<Policy, const Policy&>) {
        auto bound = algo(policy, 3, fn);
        static_assert(std::same_as<decltype(bound.__policy_), Policy>);
        auto from_bound = ex::just(10) | bound;
        auto bound_result = ex::sync_wait(std::move(from_bound));
        ASSERT_TRUE(bound_result.has_value());
        EXPECT_EQ(std::get<0>(*bound_result), 13);
    }
}

template<class Policy>
void check_native_policy(const Policy& policy) {
    check_native(ex::bulk, policy, callback<false>{});
    check_native(ex::bulk_unchunked, policy, callback<false>{});
    check_native(ex::bulk_chunked, policy, callback<true>{});
}
#endif

} // namespace

TEST(ExecutionBulkPolicy, CopyableImplementationPoliciesOwnExactCvrefCopies) {
    check_owned_categories<false, false>();
    check_owned_categories<false, true>();
    check_owned_categories<true, false>();
    check_owned_categories<true, true>();
}

TEST(ExecutionBulkPolicy, MoveOnlyImplementationPoliciesRemainBorrowed) {
    check_borrowed_categories<false, false, move_only_policy>();
    check_borrowed_categories<false, true, move_only_policy>();
    check_borrowed_categories<true, false, move_only_policy>();
    check_borrowed_categories<true, true, move_only_policy>();
}

TEST(ExecutionBulkPolicy, ImmovableImplementationPoliciesRemainBorrowed) {
    check_borrowed_categories<false, false, immovable_policy>();
    check_borrowed_categories<false, true, immovable_policy>();
    check_borrowed_categories<true, false, immovable_policy>();
    check_borrowed_categories<true, true, immovable_policy>();
}

TEST(ExecutionBulkPolicy, ExplicitCopyIsNotTheCopyConstructibleConcept) {
    bound_t<false, explicit_copy_policy> bound{explicit_copy_policy{}, 3, {}};
    auto sender = bound(ex::just(10));
    static_assert(std::same_as<stored_policy_t<decltype(sender)>, const explicit_copy_policy&>);
    EXPECT_EQ(std::addressof(sender.__policy), std::addressof(bound.__policy_));
    auto result = ex::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 13);
}

TEST(ExecutionBulkPolicy, ThrowingCopiesAreNotReplacedByBorrowing) {
    bool should_throw = false;
    bound_t<false, throwing_copy_policy> bound{throwing_copy_policy{should_throw}, 3, {}};
    should_throw = true;
    EXPECT_THROW((void)bound(ex::just(10)), policy_failure);
    EXPECT_THROW((void)(ex::just(10) | std::as_const(bound)), policy_failure);
    should_throw = false;
    auto result = ex::sync_wait(bound(ex::just(10)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 13);
}

TEST(ExecutionBulkPolicy, StandardPoliciesUsePublicFullCallsAndOwnedInitialBinding) {
#if FORGE_TEST_BULK_HAS_NATIVE_POLICIES
    check_native_policy(ex::seq);
    check_native_policy(ex::par);
    check_native_policy(ex::par_unseq);
#if FORGE_TEST_BULK_HAS_UNSEQ
    check_native_policy(ex::unseq);
#endif
#else
    GTEST_SKIP() << "Native <execution> does not declare policies in this library configuration";
#endif
}
