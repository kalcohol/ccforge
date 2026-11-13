#include <forge/execution_extensions.hpp>
#include <gtest/gtest.h>

#include "../sender_adaptor_closure_probe.hpp"
#include "../composition_probe.hpp"

#include <array>
#include <memory>
#include <tuple>
#include <utility>

namespace {
using namespace public_closure_probe;

template<category Category>
void check_forwarding() {
    call_state state;
    auto closure = traced<true>{{}, &state, 1} | traced<true>{{}, &state, 2};
    literal_sender source{7};
    static_assert(noexcept(source | as_category<Category>(closure)));
    auto&& result = source | as_category<Category>(closure);
    EXPECT_EQ(&result, &source);
    EXPECT_EQ(state.count, 2);
    EXPECT_EQ(state.order, (std::array<int, 2>{1, 2}));
    EXPECT_EQ(state.categories, (std::array<category, 2>{Category, Category}));

    state = {};
    traced<true> single{{}, &state, 9};
    auto&& direct = source | as_category<Category>(single);
    EXPECT_EQ(&direct, &source);
    EXPECT_EQ(state.count, 1);
    EXPECT_EQ(state.order[0], 9);
    EXPECT_EQ(state.categories[0], Category);
}

struct counted : ex::sender_adaptor_closure<counted> {
    composition_probe::capture_handler handler;
    explicit counted(composition_probe::capture_state& state) : handler(state) {}
    explicit counted(counted& other) : handler(other.handler) {}
    counted(const counted& other) : handler(other.handler) {}
    counted(counted&& other) : handler(std::move(other.handler)) {}
    explicit counted(const counted&& other) : handler(std::move(other.handler)) {}

    template<ex::sender S>
    auto operator()(S&& source) const {
        return handler(static_cast<S&&>(source));
    }
};

template<composition_probe::category Category>
void check_capture() {
    composition_probe::capture_state state;
    {
        counted original{state};
        auto closure = identity{} | composition_probe::as_category<Category>(original);
        EXPECT_EQ(state.alive, 2);
        EXPECT_EQ(state.constructions[static_cast<std::size_t>(Category)], 1);
        auto result = forge::sync_wait(ex::just(7) | closure);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 7);
        EXPECT_EQ(state.calls, 1);
    }
    EXPECT_EQ(state.alive, 0);
}

} // namespace

TEST(ExecutionSenderAdaptorClosure, UserClosurePipesAndComposesInBothOrders) {
    const scale twice{};
    auto increment = ex::then([](int value) noexcept { return value + 1; });
    auto left = twice | increment;
    const auto right = increment | scale{};
    auto first = forge::sync_wait(ex::just(3) | left);
    auto second = forge::sync_wait(ex::just(3) | right);
    auto third = forge::sync_wait(ex::just(3) | std::move(left));
    auto fourth = forge::sync_wait(ex::just(3) | std::move(right));
    auto fifth = forge::sync_wait(ex::just(3) | twice);
    ASSERT_TRUE(first && second && third && fourth && fifth);
    EXPECT_EQ(std::get<0>(*first), 7);
    EXPECT_EQ(std::get<0>(*second), 8);
    EXPECT_EQ(std::get<0>(*third), 7);
    EXPECT_EQ(std::get<0>(*fourth), 8);
    EXPECT_EQ(std::get<0>(*fifth), 6);
}

TEST(ExecutionSenderAdaptorClosure, ForwardingPreservesClosureCategoriesAndSenderIdentity) {
    check_forwarding<category::lvalue>();
    check_forwarding<category::const_lvalue>();
    check_forwarding<category::rvalue>();
    check_forwarding<category::const_rvalue>();
}

TEST(ExecutionSenderAdaptorClosure, CompositionCapturesFromTheOriginalCategoryOnce) {
    check_capture<composition_probe::category::lvalue>();
    check_capture<composition_probe::category::const_lvalue>();
    check_capture<composition_probe::category::rvalue>();
    check_capture<composition_probe::category::const_rvalue>();
}

TEST(ExecutionSenderAdaptorClosure, MoveOnlyClosuresAndSendersReachLibraryAdaptors) {
    auto closure = rvalue_only{} | ex::then([](std::unique_ptr<int> value) noexcept {
        return *value + 1;
    });
    auto result = forge::sync_wait(ex::just(std::make_unique<int>(41)) | std::move(closure));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ExecutionSenderAdaptorClosure, BareUnstoppableUsesThePublicMechanism) {
    auto result = forge::sync_wait(ex::just(42) | ex::unstoppable);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}
