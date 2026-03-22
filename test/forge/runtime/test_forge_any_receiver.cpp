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
