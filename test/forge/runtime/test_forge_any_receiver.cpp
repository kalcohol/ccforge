#include <gtest/gtest.h>
#include <forge/any_receiver.hpp>
#include <execution>
#include <cstddef>
#include <optional>
#include <stdexcept>

using cs_int = std::execution::completion_signatures<
    std::execution::set_value_t(int),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;
using cs_value_only = std::execution::completion_signatures<
    std::execution::set_value_t(int)>;
using cs_zero_value = std::execution::completion_signatures<
    std::execution::set_value_t()>;
using cs_error_stopped = std::execution::completion_signatures<
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;

struct test_recv {
    using receiver_concept = std::execution::receiver_t;
    int* out;
    friend void tag_invoke(std::execution::set_value_t, test_recv&& r, int v) noexcept {
        *r.out = v;
    }
    friend void tag_invoke(std::execution::set_error_t, test_recv&&, std::exception_ptr) noexcept {}
    friend void tag_invoke(std::execution::set_stopped_t, test_recv&&) noexcept {}
    friend std::execution::empty_env tag_invoke(std::execution::get_env_t, const test_recv&) noexcept {
        return {};
    }
};

struct value_only_recv {
    using receiver_concept = std::execution::receiver_t;

    int* out;

    void set_value(int value) && noexcept {
        *out = value;
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct error_stopped_recv {
    using receiver_concept = std::execution::receiver_t;

    bool* error;
    bool* stopped;

    void set_error(std::exception_ptr) && noexcept {
        *error = true;
    }

    void set_stopped() && noexcept {
        *stopped = true;
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

template<class R>
concept zero_value_completable = requires(R receiver) {
    std::execution::set_value(std::move(receiver));
};

struct receiver_move_counts {
    int moves = 0;
    int destroyed = 0;
};

struct tracking_recv {
    using receiver_concept = std::execution::receiver_t;

    receiver_move_counts* counts;
    int* out;

    tracking_recv(receiver_move_counts* c, int* value) noexcept
        : counts(c), out(value) {}

    tracking_recv(tracking_recv&& other) noexcept
        : counts(other.counts), out(other.out) {
        ++counts->moves;
    }

    tracking_recv(const tracking_recv&) = delete;

    ~tracking_recv() {
        if (counts) {
            ++counts->destroyed;
        }
    }

    void set_value(int v) && noexcept {
        *out = v;
    }

    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

struct alignas(64) over_aligned_recv {
    using receiver_concept = std::execution::receiver_t;

    receiver_move_counts* counts;
    int* out;
    char padding[64]{};

    over_aligned_recv(receiver_move_counts* c, int* value) noexcept
        : counts(c), out(value) {}

    over_aligned_recv(over_aligned_recv&& other) noexcept
        : counts(other.counts), out(other.out) {
        ++counts->moves;
    }

    over_aligned_recv(const over_aligned_recv&) = delete;

    ~over_aligned_recv() {
        if (counts) {
            ++counts->destroyed;
        }
    }

    void set_value(int v) && noexcept {
        *out = v;
    }

    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

static_assert(alignof(over_aligned_recv) > alignof(std::max_align_t));

static_assert(std::execution::receiver<forge::any_receiver_of<cs_int>>);
static_assert(std::constructible_from<
              forge::any_receiver_of<cs_int>,
              test_recv>);
static_assert(!std::constructible_from<
              forge::any_receiver_of<cs_int>,
              value_only_recv>);
static_assert(!std::execution::receiver_of<
              forge::any_receiver_of<cs_int>,
              cs_zero_value>);
static_assert(std::execution::receiver_of<
              forge::any_receiver_of<cs_value_only>,
              cs_value_only>);
static_assert(std::constructible_from<
              forge::any_receiver_of<cs_value_only>,
              value_only_recv>);
static_assert(std::execution::receiver_of<
              forge::any_receiver_of<cs_error_stopped>,
              cs_error_stopped>);
static_assert(std::constructible_from<
              forge::any_receiver_of<cs_error_stopped>,
              error_stopped_recv>);
static_assert(!zero_value_completable<
              forge::any_receiver_of<cs_error_stopped>>);

TEST(AnyReceiverTest, DefaultEmpty) {
    forge::any_receiver_of<cs_int> r;
    EXPECT_FALSE(bool(r));
}

TEST(AnyReceiverTest, HoldsConcreteReceiver) {
    int val = 0;
    forge::any_receiver_of<cs_int> r = test_recv{&val};
    EXPECT_TRUE(bool(r));
}

TEST(AnyReceiverTest, SetValueDelivered) {
    int val = 0;
    forge::any_receiver_of<cs_int> r = test_recv{&val};
    std::execution::set_value(std::move(r), 42);
    EXPECT_EQ(val, 42);
}

TEST(AnyReceiverTest, SupportsValueOnlyClosedSet) {
    int value = 0;
    forge::any_receiver_of<cs_value_only> receiver =
        value_only_recv{&value};

    std::execution::set_value(std::move(receiver), 17);

    EXPECT_EQ(value, 17);
}

TEST(AnyReceiverTest, SupportsErrorOnlyClosedSet) {
    bool error = false;
    bool stopped = false;
    forge::any_receiver_of<cs_error_stopped> receiver =
        error_stopped_recv{&error, &stopped};

    std::execution::set_error(
        std::move(receiver),
        std::make_exception_ptr(std::runtime_error{"failure"}));

    EXPECT_TRUE(error);
    EXPECT_FALSE(stopped);
}

TEST(AnyReceiverTest, SupportsStoppedOnlyDeliveryInClosedSet) {
    bool error = false;
    bool stopped = false;
    forge::any_receiver_of<cs_error_stopped> receiver =
        error_stopped_recv{&error, &stopped};

    std::execution::set_stopped(std::move(receiver));

    EXPECT_FALSE(error);
    EXPECT_TRUE(stopped);
}

TEST(AnyReceiverTest, MoveConstructsSmallObjectStorageReceiver) {
    receiver_move_counts counts;
    int val = 0;

    {
        forge::any_receiver_of<cs_int> r = tracking_recv{&counts, &val};
        EXPECT_EQ(counts.moves, 1);
        EXPECT_EQ(counts.destroyed, 1);

        forge::any_receiver_of<cs_int> moved(std::move(r));
        EXPECT_EQ(counts.moves, 2);
        EXPECT_EQ(counts.destroyed, 2);

        std::execution::set_value(std::move(moved), 99);
        EXPECT_EQ(val, 99);
    }

    EXPECT_EQ(counts.destroyed, 3);
}

TEST(AnyReceiverTest, DestroysOverAlignedHeapFallbackReceiver) {
    receiver_move_counts counts;
    int val = 0;

    {
        forge::any_receiver_of<cs_int> r =
            over_aligned_recv{&counts, &val};
        std::execution::set_value(std::move(r), 123);
        EXPECT_EQ(val, 123);
    }

    EXPECT_GE(counts.destroyed, 2);
}
