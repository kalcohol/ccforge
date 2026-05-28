#include <gtest/gtest.h>
#include <execution>
#include <atomic>
#include <thread>
#include <variant>
#include <tuple>
#include <stdexcept>

namespace {

struct throwing_move_value {
    throwing_move_value() = default;
    throwing_move_value(const throwing_move_value&) = default;
    throwing_move_value& operator=(const throwing_move_value&) = default;
    throwing_move_value(throwing_move_value&&) {
        throw std::runtime_error("move failed");
    }
    throwing_move_value& operator=(throwing_move_value&&) = delete;
};

template<class R>
struct throwing_value_op {
    using operation_state_concept = std::execution::operation_state_t;

    explicit throwing_value_op(R r) : rcvr(std::move(r)) {}
    throwing_value_op(const throwing_value_op&) = delete;
    throwing_value_op& operator=(const throwing_value_op&) = delete;
    throwing_value_op(throwing_value_op&&) = delete;
    throwing_value_op& operator=(throwing_value_op&&) = delete;

    R rcvr;

    friend void tag_invoke(std::execution::start_t, throwing_value_op& self) noexcept {
        std::execution::set_value(std::move(self.rcvr), throwing_move_value{});
    }
};

struct throwing_value_sender {
    using sender_concept = std::execution::sender_t;

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const throwing_value_sender&, auto) noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(throwing_move_value)> {
        return {};
    }

    template<std::execution::receiver R>
    friend auto tag_invoke(std::execution::connect_t, throwing_value_sender, R r)
        -> throwing_value_op<R> {
        return throwing_value_op<R>{std::move(r)};
    }

    friend auto tag_invoke(std::execution::get_env_t, const throwing_value_sender&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

} // namespace

TEST(IntoVariantTest, WrapsValue) {
    auto sndr = std::execution::into_variant(std::execution::just(42));
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    using var_t = std::variant<std::tuple<int>>;
    auto& var = std::get<0>(*result);
    EXPECT_EQ(std::get<std::tuple<int>>(var), std::make_tuple(42));
}

TEST(IntoVariantTest, ReportsConstructionFailureAsError) {
    auto sndr = std::execution::into_variant(throwing_value_sender{});
    EXPECT_THROW(std::execution::sync_wait(std::move(sndr)), std::runtime_error);
}

TEST(SyncWaitWithVariantTest, Works) {
    auto result = std::this_thread::sync_wait_with_variant(std::execution::just(42));
    ASSERT_TRUE(result.has_value());
}

TEST(BulkTest, SerialExecution) {
    int sum = 0;
    auto sndr = std::execution::just(0)
              | std::execution::bulk(5, [&sum](int idx, int& v) {
                    sum += idx; v += idx;
                });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(sum, 0+1+2+3+4);
    EXPECT_EQ(std::get<0>(*result), 0+1+2+3+4);
}

TEST(StartDetachedTest, Executes) {
    std::atomic<int> counter{0};
    std::execution::start_detached(
        std::execution::just() | std::execution::then([&counter] { counter++; }));
    EXPECT_EQ(counter.load(), 1);
}

TEST(ContinuesOnTest, TransfersToScheduler) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();
    int result = -1;
    std::thread worker([&loop] { loop.run(); });

    auto sndr = std::execution::continues_on(
                  std::execution::just(42) | std::execution::then([&result](int x) { result = x; }),
                  sch);
    std::execution::sync_wait(std::move(sndr));

    loop.finish();
    worker.join();
    EXPECT_EQ(result, 42);
}
