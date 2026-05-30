#include <gtest/gtest.h>
#include <forge/any_receiver.hpp>
#include <execution>
#include <optional>

using cs_int = std::execution::completion_signatures<
    std::execution::set_value_t(int),
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

static_assert(std::execution::receiver<forge::any_receiver_of<cs_int>>);

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
