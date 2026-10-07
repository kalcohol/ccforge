#include <gtest/gtest.h>

#include "../associate_closure_probe.hpp"

#include <array>
#include <tuple>
#include <utility>

namespace {

using namespace associate_closure_probe;

template<category Category>
void check_binding() {
    ex::simple_counting_scope scope;
    token_state state;
    tracked_token token{scope.get_token(), state};
    auto closure = ex::associate(as_category<Category>(token));
    for (std::size_t i = 0; i != state.constructions.size(); ++i) {
        EXPECT_EQ(state.constructions[i], i == static_cast<std::size_t>(Category) ? 1 : 0);
    }
    EXPECT_EQ(closure.__token.state, &state);
    EXPECT_EQ(state.wraps, 0);
    EXPECT_EQ(state.attempts, 0);
    EXPECT_EQ(scope.count(), 0u);
}

template<category Category, bool Pipe>
void check_application() {
    ex::simple_counting_scope scope;
    token_state direct_state;
    tracked_token direct_token{scope.get_token(), direct_state};
    auto direct_sender = ex::associate(ex::just(42), as_category<Category>(direct_token));
    const auto expected_constructions = direct_state.constructions;
    auto direct_result = ex::sync_wait(std::move(direct_sender));
    ASSERT_TRUE(direct_result.has_value());
    EXPECT_EQ(std::get<0>(*direct_result), 42);
    EXPECT_EQ(scope.count(), 0u);

    token_state state;
    tracked_token token{scope.get_token(), state};
    auto closure = ex::associate(token);
    state.constructions = {};
    auto sender = apply<Category, Pipe>(closure, ex::just(42));
    EXPECT_EQ(state.constructions, expected_constructions);
    // Full-form by-value initialization ignores the two explicit preferred constructors.
    EXPECT_EQ(state.constructions[0], 0);
    EXPECT_EQ(state.constructions[3], 0);
    EXPECT_EQ(state.constructions[1], Category == category::rvalue ? 0 : 1);
    EXPECT_EQ(state.constructions[2], Category == category::rvalue ? 3 : 2);
    EXPECT_EQ(state.wraps, 1);
    EXPECT_EQ(state.attempts, 1);
    EXPECT_EQ(scope.count(), 1u);
    auto result = ex::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(scope.count(), 0u);
}

template<bool Pipe>
void check_application_categories() {
    check_application<category::lvalue, Pipe>();
    check_application<category::const_lvalue, Pipe>();
    check_application<category::rvalue, Pipe>();
    check_application<category::const_rvalue, Pipe>();
}

template<class Scope, category Category, bool Pipe>
void check_scope() {
    Scope scope;
    auto closure = ex::associate(scope.get_token());
    EXPECT_EQ(scope.count(), 0u);
    {
        auto sender = apply<Category, Pipe>(closure, ex::just(23));
        EXPECT_EQ(scope.count(), 1u);
        auto result = ex::sync_wait(std::move(sender));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 23);
    }
    EXPECT_EQ(scope.count(), 0u);
}

template<class Scope>
void check_scope_categories() {
    check_scope<Scope, category::lvalue, false>();
    check_scope<Scope, category::const_lvalue, false>();
    check_scope<Scope, category::rvalue, false>();
    check_scope<Scope, category::const_rvalue, false>();
    check_scope<Scope, category::lvalue, true>();
    check_scope<Scope, category::const_lvalue, true>();
    check_scope<Scope, category::rvalue, true>();
    check_scope<Scope, category::const_rvalue, true>();
}

} // namespace

TEST(ExecutionAssociateClosure, BindingOwnsTheExactTokenCategoryWithoutAssociating) {
    check_binding<category::lvalue>();
    check_binding<category::const_lvalue>();
    check_binding<category::rvalue>();
    check_binding<category::const_rvalue>();
}

TEST(ExecutionAssociateClosure, DirectApplicationForwardsAllFourCategories) {
    check_application_categories<false>();
}

TEST(ExecutionAssociateClosure, PipeApplicationForwardsAllFourCategories) {
    check_application_categories<true>();
}

TEST(ExecutionAssociateClosure, SimpleScopeAssociationEndsWithOperationLifetime) {
    check_scope_categories<ex::simple_counting_scope>();
}

TEST(ExecutionAssociateClosure, CountingScopeAssociationEndsWithOperationLifetime) {
    check_scope_categories<ex::counting_scope>();
}

TEST(ExecutionAssociateClosure, ClosingAfterBindingMakesApplicationCompleteStopped) {
    ex::simple_counting_scope scope;
    auto closure = ex::associate(scope.get_token());
    scope.close();
    auto result = ex::sync_wait(ex::just(42) | closure);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(scope.count(), 0u);
}

TEST(ExecutionAssociateClosure, WrappingExceptionsPropagateOnlyAtApplication) {
    ex::simple_counting_scope scope;
    token_state state;
    tracked_token token{scope.get_token(), state};
    state.throw_wrap = true;
    auto closure = ex::associate(token);
    EXPECT_EQ(state.wraps, 0);
    EXPECT_THROW((void)(ex::just(42) | closure), wrap_failure);
    EXPECT_EQ(state.wraps, 1);
    EXPECT_EQ(state.attempts, 0);
    EXPECT_EQ(scope.count(), 0u);
}

TEST(ExecutionAssociateClosure, AssociationExceptionsPropagateOnlyAtApplication) {
    ex::simple_counting_scope scope;
    token_state state;
    tracked_token token{scope.get_token(), state};
    state.throw_associate = true;
    auto closure = ex::associate(token);
    EXPECT_EQ(state.attempts, 0);
    EXPECT_THROW((void)closure(ex::just(42)), association_failure);
    EXPECT_EQ(state.wraps, 1);
    EXPECT_EQ(state.attempts, 1);
    EXPECT_EQ(scope.count(), 0u);
}
