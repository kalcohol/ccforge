#include <gtest/gtest.h>
#include <forge/any_sender.hpp>
#include <execution>
#include <cstddef>
#include <stdexcept>
#include <tuple>
#include <type_traits>

using cs_int = std::execution::completion_signatures<
    std::execution::set_value_t(int),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;

using int_sender_t = decltype(std::execution::just(1));
using long_long_sender_t = decltype(std::execution::just(1LL));
using zero_sender_t = decltype(std::execution::just());

static_assert(std::constructible_from<
              forge::any_sender_of<cs_int>,
              int_sender_t>);
static_assert(!std::constructible_from<
              forge::any_sender_of<cs_int>,
              long_long_sender_t>);
static_assert(!std::constructible_from<
              forge::any_sender_of<cs_int>,
              zero_sender_t>);

struct sender_move_counts {
    int moves = 0;
    int destroyed = 0;
};

struct rvalue_connect_only_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> cs_int {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;

        void start() & noexcept {
            std::execution::set_value(std::move(rcvr), 5);
        }
    };

    template<std::execution::receiver R>
    auto connect(R rcvr) && -> op<R> {
        return op<R>{std::move(rcvr)};
    }
};

constexpr int select_sender(rvalue_connect_only_sender) {
    return 1;
}

constexpr int select_sender(forge::any_sender_of<cs_int>) {
    return 2;
}

static_assert(std::execution::sender<rvalue_connect_only_sender>);
static_assert(!std::constructible_from<
              forge::any_sender_of<cs_int>,
              rvalue_connect_only_sender>);
static_assert(select_sender(rvalue_connect_only_sender{}) == 1);

struct tracking_sender {
    using sender_concept = std::execution::sender_t;

    sender_move_counts* counts;
    int value;

    tracking_sender(sender_move_counts* c, int v) noexcept
        : counts(c), value(v) {}

    tracking_sender(tracking_sender&& other) noexcept
        : counts(other.counts), value(other.value) {
        ++counts->moves;
    }

    tracking_sender(const tracking_sender&) = delete;

    ~tracking_sender() {
        if (counts) {
            ++counts->destroyed;
        }
    }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> cs_int {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        int value;
        R rcvr;

        void start() & noexcept {
            std::execution::set_value(std::move(rcvr), value);
        }
    };

    template<std::execution::receiver R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{value, std::move(rcvr)};
    }
};

struct throwing_move_sender {
    using sender_concept = std::execution::sender_t;

    sender_move_counts* counts;
    int value;

    throwing_move_sender(sender_move_counts* c, int v) noexcept
        : counts(c), value(v) {}

    throwing_move_sender(throwing_move_sender&& other)
        : counts(other.counts), value(other.value) {
        ++counts->moves;
    }

    throwing_move_sender(const throwing_move_sender&) = delete;

    ~throwing_move_sender() {
        if (counts) {
            ++counts->destroyed;
        }
    }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> cs_int {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        int value;
        R rcvr;

        void start() & noexcept {
            std::execution::set_value(std::move(rcvr), value);
        }
    };

    template<std::execution::receiver R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{value, std::move(rcvr)};
    }
};

static_assert(!std::is_nothrow_move_constructible_v<throwing_move_sender>);

struct alignas(64) over_aligned_sender {
    using sender_concept = std::execution::sender_t;

    sender_move_counts* counts;
    int value;
    char padding[64]{};

    over_aligned_sender(sender_move_counts* c, int v) noexcept
        : counts(c), value(v) {}

    over_aligned_sender(over_aligned_sender&& other) noexcept
        : counts(other.counts), value(other.value) {
        ++counts->moves;
    }

    over_aligned_sender(const over_aligned_sender&) = delete;

    ~over_aligned_sender() {
        if (counts) {
            ++counts->destroyed;
        }
    }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> cs_int {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        int value;
        R rcvr;

        void start() & noexcept {
            std::execution::set_value(std::move(rcvr), value);
        }
    };

    template<std::execution::receiver R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{value, std::move(rcvr)};
    }
};

static_assert(alignof(over_aligned_sender) > alignof(std::max_align_t));

TEST(AnySenderTest, DefaultEmptyThrowsOnSyncWait) {
    forge::any_sender_of<cs_int> erased;

    EXPECT_FALSE(bool(erased));
    EXPECT_THROW((void)erased.sync_wait(), std::runtime_error);
}

TEST(AnySenderTest, SyncWaitRunsStoredSender) {
    forge::any_sender_of<cs_int> erased = std::execution::just(42);

    auto result = erased.sync_wait();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(AnySenderTest, MoveConstructsSmallObjectStorageSender) {
    sender_move_counts counts;

    {
        forge::any_sender_of<cs_int> erased = tracking_sender{&counts, 17};
        EXPECT_EQ(counts.moves, 1);
        EXPECT_EQ(counts.destroyed, 1);

        forge::any_sender_of<cs_int> moved(std::move(erased));
        EXPECT_EQ(counts.moves, 2);
        EXPECT_EQ(counts.destroyed, 2);

        auto result = moved.sync_wait();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 17);
    }

    EXPECT_GE(counts.destroyed, 3);
}

TEST(AnySenderTest, MoveAssignsSmallObjectStorageSender) {
    sender_move_counts counts;

    {
        forge::any_sender_of<cs_int> first = tracking_sender{&counts, 21};
        forge::any_sender_of<cs_int> second = std::execution::just(1);

        second = std::move(first);
        EXPECT_FALSE(bool(first));

        auto result = second.sync_wait();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 21);
    }

    EXPECT_GE(counts.destroyed, 3);
}

TEST(AnySenderTest, ThrowingMoveSmallSenderUsesHeapFallback) {
    sender_move_counts counts;

    {
        forge::any_sender_of<cs_int> erased =
            throwing_move_sender{&counts, 29};
        EXPECT_EQ(counts.moves, 1);
        EXPECT_EQ(counts.destroyed, 1);

        forge::any_sender_of<cs_int> moved(std::move(erased));
        EXPECT_FALSE(bool(erased));
        EXPECT_EQ(counts.moves, 1);

        auto result = moved.sync_wait();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 29);
    }

    EXPECT_GE(counts.destroyed, 2);
}

TEST(AnySenderTest, DestroysOverAlignedHeapFallbackSender) {
    sender_move_counts counts;

    {
        forge::any_sender_of<cs_int> erased =
            over_aligned_sender{&counts, 33};
        auto result = erased.sync_wait();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 33);
    }

    EXPECT_GE(counts.destroyed, 2);
}
