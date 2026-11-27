#include <forge/bulk.hpp>
#include <execution>
#include <gtest/gtest.h>
#include "native_policy_config.hpp"
#include "observe_completion.hpp"

#include <array>
#include <concepts>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace ex = std::execution;

namespace {

using values = ex::completion_signatures<ex::set_value_t(int)>;

template<class S>
void expect_value(S sndr, int expected) {
    static_assert(std::same_as<ex::completion_signatures_of_t<S, ex::env<>>, values>);
    const auto result = std::this_thread::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), expected);
}

auto each_index() {
    return [](int index, int& value) noexcept { value = value * 10 + index; };
}

auto each_chunk(std::array<int, 2>& range) {
    return [&range](int first, int last, int& value) noexcept {
        range = {first, last};
        value += last - first;
    };
}

struct stateful_index {
    int digit;
    int* copies;
    int* moves;

    stateful_index(int initial, int& copy_count, int& move_count) noexcept
        : digit(initial), copies(&copy_count), moves(&move_count) {}
    stateful_index(const stateful_index& other) noexcept
        : digit(other.digit), copies(other.copies), moves(other.moves) { ++*copies; }
    stateful_index(stateful_index&& other) noexcept(false)
        : digit(other.digit), copies(other.copies), moves(other.moves) { ++*moves; }

    void operator()(int index, int& value) const noexcept {
        value = value * 10 + digit + index;
    }
};

} // namespace

TEST(ForgeBulk, DirectAndPipeVisitIndexesInOrder) {
    expect_value(forge::bulk(ex::just(1), 4, each_index()), 10123);
    expect_value(ex::just(1) | forge::bulk(4, each_index()), 10123);
    expect_value(forge::bulk_unchunked(ex::just(1), 4, each_index()), 10123);
    expect_value(ex::just(1) | forge::bulk_unchunked(4, each_index()), 10123);
}

TEST(ForgeBulk, DirectAndPipeUseOneSerialChunk) {
    std::array<int, 2> range{-1, -1};
    expect_value(forge::bulk_chunked(ex::just(1), 4, each_chunk(range)), 5);
    EXPECT_EQ(range, (std::array{0, 4}));
    range = {-1, -1};
    expect_value(ex::just(1) | forge::bulk_chunked(4, each_chunk(range)), 5);
    EXPECT_EQ(range, (std::array{0, 4}));
}

TEST(ForgeBulk, BindingCopiesCallbackStateWithoutMovingIt) {
    int copies = 0;
    int moves = 0;
    stateful_index callback(2, copies, moves);
    int shape = 3;
    static_assert(noexcept(forge::bulk(shape, callback)));
    static_assert(!noexcept(forge::bulk(shape, std::move(callback))));
    auto bound = forge::bulk(shape, callback);
    EXPECT_EQ(copies, 1);
    EXPECT_EQ(moves, 0);
    callback.digit = 6;
    shape = 1;
    expect_value(ex::just(1) | bound, 1234);
    expect_value(ex::just(1) | std::as_const(bound), 1234);
    expect_value(ex::just(1) | std::move(std::as_const(bound)), 1234);
}

TEST(ForgeBulk, ForwardsStoppedAndCallbackErrors) {
    bool called = false;
    auto stopped = forge::bulk(ex::just_stopped(), 4, [&](int, int&) noexcept { called = true; });
    EXPECT_TRUE(forge_test::observe_completion(std::move(stopped)).stopped);
    EXPECT_FALSE(called);
    auto error = ex::just(1) | forge::bulk_unchunked(1, [](int, int&) {
        throw std::runtime_error("bulk callback");
    });
    auto result = forge_test::observe_completion(std::move(error));
    ASSERT_TRUE(result.error);
    EXPECT_THROW(std::rethrow_exception(*result.error), std::runtime_error);
}
