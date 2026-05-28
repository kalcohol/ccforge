#include <gtest/gtest.h>
#include <execution>
#include <exception>
#include <tuple>
#include <string>
#include <type_traits>

namespace {

struct multi_value_sender {
    using sender_concept = std::execution::sender_t;

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const multi_value_sender&, auto) noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_value_t(double),
            std::execution::set_error_t(short)> {
        return {};
    }

    friend auto tag_invoke(std::execution::get_env_t, const multi_value_sender&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

struct other_multi_value_sender {
    using sender_concept = std::execution::sender_t;

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const other_multi_value_sender&, auto) noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(char),
            std::execution::set_value_t(bool),
            std::execution::set_error_t(long),
            std::execution::set_stopped_t()> {
        return {};
    }

    friend auto tag_invoke(std::execution::get_env_t, const other_multi_value_sender&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

struct scheduler_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::execution::inline_scheduler expected;
    bool* got_scheduler;
    bool* completed;

    friend void tag_invoke(std::execution::set_value_t, scheduler_receiver&& self,
                           std::execution::inline_scheduler sch) noexcept {
        *self.got_scheduler = (sch == self.expected);
        *self.completed = true;
    }

    template<class E>
    friend void tag_invoke(std::execution::set_error_t, scheduler_receiver&& self, E&&) noexcept {
        *self.completed = false;
    }

    friend void tag_invoke(std::execution::set_stopped_t, scheduler_receiver&& self) noexcept {
        *self.completed = false;
    }

    friend auto tag_invoke(std::execution::get_env_t, const scheduler_receiver& self) noexcept {
        return std::execution::make_env(
            std::execution::make_prop(std::execution::get_scheduler_t{}, self.expected));
    }
};

struct int_error_receiver {
    using receiver_concept = std::execution::receiver_t;

    bool* got_int;
    bool* got_exception_ptr;

    template<class... Vs>
    friend void tag_invoke(std::execution::set_value_t, int_error_receiver&&, Vs&&...) noexcept {}

    friend void tag_invoke(std::execution::set_error_t, int_error_receiver&& self, int) noexcept {
        *self.got_int = true;
    }

    friend void tag_invoke(std::execution::set_error_t, int_error_receiver&& self,
                           std::exception_ptr) noexcept {
        *self.got_exception_ptr = true;
    }

    friend void tag_invoke(std::execution::set_stopped_t, int_error_receiver&&) noexcept {}

    friend auto tag_invoke(std::execution::get_env_t, const int_error_receiver&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

} // namespace

TEST(WhenAllTest, AggregatesMultipleValues) {
    auto sndr = std::execution::when_all(
        std::execution::just(1),
        std::execution::just(2.0));
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 1);
    EXPECT_DOUBLE_EQ(std::get<1>(*result), 2.0);
}

TEST(WhenAllTest, SingleSender) {
    auto sndr = std::execution::when_all(std::execution::just(42));
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(WhenAllTest, ErrorPropagates) {
    auto sndr = std::execution::when_all(
        std::execution::just(1),
        std::execution::just_error(42));
    EXPECT_THROW(std::execution::sync_wait(std::move(sndr)), int);
}

TEST(WhenAllTest, ReportsCartesianValueAndChildErrorSignatures) {
    auto sndr = std::execution::when_all(
        multi_value_sender{},
        other_multi_value_sender{});
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int, char),
            std::execution::set_value_t(int, bool),
            std::execution::set_value_t(double, char),
            std::execution::set_value_t(double, bool),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_error_t(short),
            std::execution::set_error_t(long),
            std::execution::set_stopped_t()>>);
}

TEST(WhenAllTest, DropsValueSignatureWhenAChildCannotProduceValue) {
    auto sndr = std::execution::when_all(
        std::execution::just_error(42),
        std::execution::just(2));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_error_t(int)>>);
}

TEST(WhenAllTest, ChildEnvPreservesOuterQueries) {
    std::execution::inline_scheduler sch;
    bool got_scheduler = false;
    bool completed = false;

    auto op = std::execution::connect(
        std::execution::when_all(std::execution::read_env(std::execution::get_scheduler)),
        scheduler_receiver{sch, &got_scheduler, &completed});

    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(got_scheduler);
}

TEST(WhenAllTest, ForwardsOriginalErrorType) {
    bool got_int = false;
    bool got_exception_ptr = false;

    auto op = std::execution::connect(
        std::execution::when_all(std::execution::just_error(42)),
        int_error_receiver{&got_int, &got_exception_ptr});

    std::execution::start(op);

    EXPECT_TRUE(got_int);
    EXPECT_FALSE(got_exception_ptr);
}

TEST(WhenAllTest, StoppedPropagates) {
    auto sndr = std::execution::when_all(
        std::execution::just(1),
        std::execution::just_stopped());
    auto result = std::execution::sync_wait(std::move(sndr));
    EXPECT_FALSE(result.has_value());
}
