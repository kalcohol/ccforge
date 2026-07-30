#include <gtest/gtest.h>
#include <forge/any_scheduler.hpp>
#include <forge/static_thread_pool.hpp>
#include "forge_operation_destroy.hpp"
#include <execution>
#include <memory>
#include <stdexcept>
#include <tuple>

static_assert(std::execution::scheduler<forge::any_scheduler>);

namespace {

struct scheduler_counts {
    int copied = 0;
    int moved = 0;
    int destroyed = 0;
};

struct tracking_scheduler {
    using scheduler_concept = std::execution::scheduler_t;

    forge::static_thread_pool* pool;
    std::shared_ptr<scheduler_counts> counts;

    tracking_scheduler(
        forge::static_thread_pool* p,
        std::shared_ptr<scheduler_counts> c) noexcept
        : pool(p), counts(std::move(c)) {}

    tracking_scheduler(const tracking_scheduler& other) noexcept
        : pool(other.pool), counts(other.counts) {
        ++counts->copied;
    }

    tracking_scheduler(tracking_scheduler&& other) noexcept
        : pool(other.pool), counts(std::move(other.counts)) {
        if (counts) {
            ++counts->moved;
        }
    }

    ~tracking_scheduler() {
        if (counts) {
            ++counts->destroyed;
        }
    }

    auto schedule() const noexcept {
        return std::execution::schedule(pool->get_scheduler());
    }

    friend bool operator==(
        const tracking_scheduler& lhs,
        const tracking_scheduler& rhs) noexcept {
        return lhs.pool == rhs.pool;
    }
};

static_assert(std::execution::scheduler<tracking_scheduler>);

struct self_destroying_schedule_receiver {
    using receiver_concept = std::execution::receiver_t;

    forge_test::destroy_context_base* context = nullptr;
    bool* completed = nullptr;

    void set_value() && noexcept {
        *completed = true;
        context->destroy();
    }

    void set_error(std::exception_ptr) && noexcept {
        *completed = true;
        context->destroy();
    }

    void set_stopped() && noexcept {
        *completed = true;
        context->destroy();
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

struct stop_aware_schedule_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::inplace_stop_source* source = nullptr;
    bool* value = nullptr;
    bool* stopped = nullptr;

    void set_value() && noexcept {
        *value = true;
    }

    void set_error(std::exception_ptr) && noexcept {}

    void set_stopped() && noexcept {
        *stopped = true;
    }

    auto get_env() const noexcept {
        return std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_stop_token_t{},
                source->get_token()));
    }
};

struct error_category_receiver {
    using receiver_concept = std::execution::receiver_t;

    int* selected_overload = nullptr;

    void set_value() && noexcept {
        *selected_overload = -1;
    }

    void set_error(const std::exception_ptr&) && noexcept {
        *selected_overload = 1;
    }

    void set_error(std::exception_ptr&&) && noexcept {
        *selected_overload = 2;
    }

    void set_stopped() && noexcept {
        *selected_overload = -1;
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

} // namespace

TEST(AnySchedulerTest, DefaultEmpty) {
    forge::any_scheduler scheduler;

    EXPECT_FALSE(bool(scheduler));
    EXPECT_TRUE(scheduler == forge::any_scheduler{});
}

TEST(AnySchedulerTest, EmptyScheduleCompletesWithError) {
    forge::any_scheduler scheduler;

    EXPECT_THROW(
        (void)std::execution::sync_wait(std::execution::schedule(scheduler)),
        std::runtime_error);
}

TEST(AnySchedulerTest, EmptyScheduleDeliversDeclaredErrorCategory) {
    forge::any_scheduler scheduler;
    int selected_overload = 0;
    auto op = std::execution::connect(
        std::execution::schedule(scheduler),
        error_category_receiver{&selected_overload});

    std::execution::start(op);

    EXPECT_EQ(selected_overload, 2);
}

TEST(AnySchedulerTest, WrappedThreadPoolSchedulerRunsWork) {
    forge::static_thread_pool pool{1};
    forge::any_scheduler scheduler{pool.get_scheduler()};

    auto result = std::execution::sync_wait(
        std::execution::schedule(scheduler)
        | std::execution::then([] { return 42; }));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(AnySchedulerTest, CopyAndMoveShareIdentity) {
    forge::static_thread_pool pool{1};
    forge::any_scheduler scheduler{pool.get_scheduler()};
    forge::any_scheduler copied = scheduler;
    forge::any_scheduler moved = std::move(scheduler);

    EXPECT_FALSE(bool(scheduler));
    EXPECT_TRUE(copied == moved);

    auto result = std::execution::sync_wait(std::execution::schedule(copied));
    EXPECT_TRUE(result.has_value());
}

TEST(AnySchedulerTest, DistinctErasedSchedulersHaveDistinctIdentity) {
    forge::static_thread_pool pool{1};
    auto concrete = pool.get_scheduler();
    forge::any_scheduler first{concrete};
    forge::any_scheduler second{concrete};

    EXPECT_FALSE(first == second);
}

TEST(AnySchedulerTest, ShutdownUnderlyingPoolCompletesStopped) {
    forge::static_thread_pool pool{1};
    forge::any_scheduler scheduler{pool.get_scheduler()};
    pool.shutdown();

    auto result = std::execution::sync_wait(std::execution::schedule(scheduler));

    EXPECT_FALSE(result.has_value());
}

TEST(AnySchedulerTest, PropagatesReceiverStopTokenThroughErasedReceiver) {
    forge::static_thread_pool pool{1};
    forge::any_scheduler scheduler{pool.get_scheduler()};
    std::inplace_stop_source stop_source;
    bool value = false;
    bool stopped = false;

    auto op = std::execution::connect(
        std::execution::schedule(scheduler),
        stop_aware_schedule_receiver{&stop_source, &value, &stopped});
    EXPECT_TRUE(stop_source.request_stop());

    std::execution::start(op);

    EXPECT_FALSE(value);
    EXPECT_TRUE(stopped);
}

TEST(AnySchedulerTest, TrackingSchedulerLifetimeIsShared) {
    forge::static_thread_pool pool{1};
    auto counts = std::make_shared<scheduler_counts>();

    {
        tracking_scheduler concrete{&pool, counts};
        forge::any_scheduler erased{concrete};
        forge::any_scheduler copied = erased;

        EXPECT_EQ(counts->copied, 1);
        EXPECT_TRUE(erased == copied);

        auto result = std::execution::sync_wait(std::execution::schedule(copied));
        EXPECT_TRUE(result.has_value());
    }

    EXPECT_EQ(counts->destroyed, 2);
}

TEST(AnySchedulerTest, SynchronousScheduleAllowsReceiverToDestroyOperation) {
    forge::any_scheduler scheduler{std::execution::inline_scheduler{}};
    auto sender = std::execution::schedule(scheduler);

    using sender_t = decltype(sender);
    using receiver_t = self_destroying_schedule_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t>(),
        std::declval<receiver_t>()));

    bool completed = false;
    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};

    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            std::move(sender),
            self_destroying_schedule_receiver{&context, &completed});
    });
    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);
}
