#include <gtest/gtest.h>

#include "../composition_probe.hpp"

#include <array>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>

namespace {

using namespace composition_probe;

template<category Category, bool Pipe>
void check_calls() {
    call_state state;
    core<category_handler<true>, category_handler<true>> closure{
        category_handler<true>{&state, 1}, category_handler<true>{&state, 2}};
    auto source = ex::just(7);
    auto&& result = [&]() -> decltype(auto) {
        if constexpr (Pipe) return source | as_category<Category>(closure);
        else return as_category<Category>(closure)(source);
    }();
    EXPECT_EQ(std::addressof(result), std::addressof(source));
    EXPECT_EQ(state.count, 2);
    EXPECT_EQ(state.order, (std::array<int, 2>{1, 2}));
    EXPECT_EQ(state.categories, (std::array<category, 2>{Category, Category}));
}

template<bool Pipe>
void check_call_categories() {
    check_calls<category::lvalue, Pipe>();
    check_calls<category::const_lvalue, Pipe>();
    check_calls<category::rvalue, Pipe>();
    check_calls<category::const_rvalue, Pipe>();
}

template<category FirstCategory, category SecondCategory>
void check_capture() {
    capture_state first_state;
    capture_state second_state;
    {
        capture_handler first{first_state};
        capture_handler second{second_state};
        {
            core<capture_handler, capture_handler> closure{
                as_category<FirstCategory>(first), as_category<SecondCategory>(second)};
            for (std::size_t i = 0; i != first_state.constructions.size(); ++i) {
                EXPECT_EQ(first_state.constructions[i], i == static_cast<std::size_t>(FirstCategory) ? 1 : 0);
                EXPECT_EQ(second_state.constructions[i], i == static_cast<std::size_t>(SecondCategory) ? 1 : 0);
            }
            EXPECT_EQ(first_state.alive, 2);
            EXPECT_EQ(second_state.alive, 2);
            EXPECT_EQ(first_state.calls, 0);
            EXPECT_EQ(second_state.calls, 0);
        }
        EXPECT_EQ(first_state.alive, 1);
        EXPECT_EQ(second_state.alive, 1);
    }
    EXPECT_EQ(first_state.alive, 0);
    EXPECT_EQ(second_state.alive, 0);
}

template<category FirstCategory>
void check_second_capture_categories() {
    check_capture<FirstCategory, category::lvalue>();
    check_capture<FirstCategory, category::const_lvalue>();
    check_capture<FirstCategory, category::rvalue>();
    check_capture<FirstCategory, category::const_rvalue>();
}

} // namespace

TEST(ExecutionComposition, NestedClosuresRunLeftToRight) {
    auto closure = ex::then([](int value) noexcept { return value + 2; })
        | ex::then([](int value) noexcept { return value * 3; })
        | ex::then([](int value) noexcept { return value - 1; });
    auto direct = ex::sync_wait(closure(ex::just(7)));
    auto piped = ex::sync_wait(ex::just(7) | closure);
    ASSERT_TRUE(direct.has_value());
    ASSERT_TRUE(piped.has_value());
    EXPECT_EQ(std::get<0>(*direct), 26);
    EXPECT_EQ(std::get<0>(*piped), 26);
}

TEST(ExecutionComposition, DirectCallPreservesBothStateCategoriesAndReferences) {
    check_call_categories<false>();
}

TEST(ExecutionComposition, PipeCallPreservesBothStateCategoriesAndReferences) {
    check_call_categories<true>();
}

TEST(ExecutionComposition, CoreDirectInitializationCoversAllSixteenCapturePairs) {
    check_second_capture_categories<category::lvalue>();
    check_second_capture_categories<category::const_lvalue>();
    check_second_capture_categories<category::rvalue>();
    check_second_capture_categories<category::const_rvalue>();
}

TEST(ExecutionComposition, ThrowingSecondCaptureDestroysTheFirstCapture) {
    capture_state first_state;
    capture_state second_state;
    capture_handler first{first_state};
    capture_handler second{second_state};
    second_state.throw_on[0] = true;
    EXPECT_THROW((core<capture_handler, capture_handler>{first, second}), capture_failure);
    EXPECT_EQ(first_state.constructions, (std::array<int, 4>{1, 0, 0, 0}));
    EXPECT_EQ(second_state.constructions, (std::array<int, 4>{1, 0, 0, 0}));
    EXPECT_EQ(first_state.alive, 1);
    EXPECT_EQ(second_state.alive, 1);
    EXPECT_EQ(first_state.calls, 0);
    EXPECT_EQ(second_state.calls, 0);
}

TEST(ExecutionComposition, ThrowingFirstCallDoesNotInvokeTheSecond) {
    call_state state;
    bool throw_now = true;
    core<category_handler<false>, category_handler<true>> closure{
        category_handler<false>{&state, 1, &throw_now}, category_handler<true>{&state, 2}};
    auto source = ex::just(7);
    EXPECT_THROW((void)(source | closure), call_failure);
    EXPECT_EQ(state.count, 1);
    EXPECT_EQ(state.order[0], 1);
}

TEST(ExecutionComposition, ThrowingSecondCallPropagatesAfterTheFirst) {
    call_state state;
    bool throw_now = true;
    core<category_handler<true>, category_handler<false>> closure{
        category_handler<true>{&state, 1}, category_handler<false>{&state, 2, &throw_now}};
    auto source = ex::just(7);
    EXPECT_THROW((void)closure(source), call_failure);
    EXPECT_EQ(state.count, 2);
    EXPECT_EQ(state.order, (std::array<int, 2>{1, 2}));
}

TEST(ExecutionComposition, MoveOnlyCapturesAndPayloadRemainUsableAsRvalues) {
    auto first = ex::then([offset = std::make_unique<int>(2)](std::unique_ptr<int> value) noexcept {
        *value += *offset;
        return value;
    });
    auto second = ex::then([factor = std::make_unique<int>(3)](std::unique_ptr<int> value) noexcept {
        return *value * *factor;
    });
    auto closure = std::move(first) | std::move(second);
    auto result = ex::sync_wait(ex::just(std::make_unique<int>(7)) | std::move(closure));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 27);
}

TEST(ExecutionComposition, MixedErrorAndStoppedAdaptorsKeepTheirCompletionPaths) {
    auto error_closure = ex::upon_error([](int error) noexcept { return error + 1; })
        | ex::then([](int value) noexcept { return value * 2; });
    auto error_result = ex::sync_wait(ex::just_error(7) | error_closure);
    ASSERT_TRUE(error_result.has_value());
    EXPECT_EQ(std::get<0>(*error_result), 16);
    auto stopped_closure = ex::upon_stopped([]() noexcept { return 11; })
        | ex::let_value([](int value) { return ex::just(value + 1); });
    auto stopped_result = ex::sync_wait(ex::just_stopped() | stopped_closure);
    ASSERT_TRUE(stopped_result.has_value());
    EXPECT_EQ(std::get<0>(*stopped_result), 12);
}

TEST(ExecutionComposition, SingleArgumentCposComposeWithBoundClosures) {
    auto closure = ex::then([](int value) noexcept { return value + 2; })
        | ex::into_variant | ex::stopped_as_optional
        | ex::then([](const auto& value) noexcept { return std::get<0>(std::get<0>(*value)) * 2; });
    auto result = ex::sync_wait(ex::just(3) | closure);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 10);
}

TEST(ExecutionComposition, BulkCompositionKeepsNonpositiveShapesEmpty) {
    for (int shape : {3, 0, -1}) {
        int calls = 0;
        auto closure = ex::bulk(shape, [&calls](int index, int& value) noexcept { ++calls; value += index; })
            | ex::then([](int value) noexcept { return value * 2; });
        auto result = ex::sync_wait(ex::just(7) | closure);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), shape > 0 ? 20 : 14);
        EXPECT_EQ(calls, shape > 0 ? 3 : 0);
    }
}

TEST(ExecutionComposition, AssociateCompositionDoesNotAcquireDuringBinding) {
    ex::simple_counting_scope scope;
    auto closure = ex::associate(scope.get_token()) | ex::then(value_callback{2});
    EXPECT_EQ(scope.count(), 0u);
    {
        auto sender = ex::just(7) | closure;
        EXPECT_EQ(scope.count(), 1u);
        auto result = ex::sync_wait(std::move(sender));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 9);
    }
    EXPECT_EQ(scope.count(), 0u);
}
