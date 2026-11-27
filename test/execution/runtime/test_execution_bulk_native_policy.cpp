// Public bulk calls with the native execution policy objects. This source is
// registered only in a mode whose native library provides the objects, and it
// does not compile anywhere else, so the test cannot pass without its cases.
#include <execution>
#include <gtest/gtest.h>

#include "../bulk_policy_probe.hpp"
#include "native_policy_config.hpp"

#include <concepts>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(FORGE_NATIVE_EXECUTION_POLICY_OBJECTS,
    "Register this source only where native execution policy objects exist");
// The pipe form needs a copyable policy ([exec.adapt.obj]). Where the native
// policies are not copyable, a configure probe pins the rejection instead.
static_assert(std::constructible_from<std::execution::parallel_policy, const std::execution::parallel_policy&> ==
              bool(FORGE_TEST_EXPECT_POLICY_COPYABLE));

namespace {

using namespace bulk_policy_probe;

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
    auto result = std::this_thread::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 13);
    if constexpr (std::constructible_from<Policy, const Policy&>) {
        auto bound = algo(policy, 3, fn);
        static_assert(std::same_as<decltype(bound.__policy_), Policy>);
        auto from_bound = ex::just(10) | bound;
        auto bound_result = std::this_thread::sync_wait(std::move(from_bound));
        ASSERT_TRUE(bound_result.has_value());
        EXPECT_EQ(std::get<0>(*bound_result), 13);
    }
}

template<class Policy>
void check_native_policy(const Policy& policy) {
    static_assert(std::is_execution_policy_v<Policy>);
    check_native(ex::bulk, policy, callback<false>{});
    check_native(ex::bulk_unchunked, policy, callback<false>{});
    check_native(ex::bulk_chunked, policy, callback<true>{});
}

} // namespace

TEST(ExecutionBulkNativePolicy, Seq) { check_native_policy(ex::seq); }
TEST(ExecutionBulkNativePolicy, Par) { check_native_policy(ex::par); }
TEST(ExecutionBulkNativePolicy, ParUnseq) { check_native_policy(ex::par_unseq); }
#if FORGE_NATIVE_EXECUTION_POLICY_UNSEQ
TEST(ExecutionBulkNativePolicy, Unseq) { check_native_policy(ex::unseq); }
#endif

TEST(ExecutionBulkNativePolicy, UnchunkedVisitsEveryIndexInOrder) {
    std::vector<int> indexes;

    auto sndr = ex::bulk_unchunked(ex::just(5), ex::seq, 3, [&indexes](int index, int& value) {
        indexes.push_back(index);
        value += index;
    });
    auto result = std::this_thread::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(indexes, (std::vector<int>{0, 1, 2}));
    EXPECT_EQ(std::get<0>(*result), 8);
}

TEST(ExecutionBulkNativePolicy, ChunkedUsesOneSerialChunk) {
    int calls = 0;

    auto sndr = ex::bulk_chunked(ex::just(2), ex::par, 4, [&calls](int begin, int end, int& value) {
        ++calls;
        value += end - begin;
    });
    auto result = std::this_thread::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(std::get<0>(*result), 6);
}
