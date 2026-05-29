#include <gtest/gtest.h>
#include <execution>
#include <optional>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <exception>

namespace {

struct throwing_query {
    template<class Env>
    int operator()(const Env&) const {
        throw std::runtime_error("read_env query failed");
    }
};

struct throws_on_copy {
    throws_on_copy() = default;
    throws_on_copy(const throws_on_copy&) {
        throw std::runtime_error("optional value construction failed");
    }
};

struct throwing_value_sender {
    using sender_concept = std::execution::sender_t;

    template<std::execution::receiver R>
    struct op : std::execution::__forge_detail::__immovable {
        using operation_state_concept = std::execution::operation_state_t;
        R rcvr;
        throws_on_copy value;

        explicit op(R r) : rcvr(std::move(r)) {}

        friend void tag_invoke(std::execution::start_t, op& self) noexcept {
            std::execution::set_value(std::move(self.rcvr), self.value);
        }
    };

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const throwing_value_sender&, auto) noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(const throws_on_copy&)> {
        return {};
    }

    template<std::execution::receiver R>
    friend auto tag_invoke(std::execution::connect_t, throwing_value_sender, R r)
        -> op<R> {
        return op<R>{std::move(r)};
    }

    friend auto tag_invoke(std::execution::get_env_t, const throwing_value_sender&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

} // namespace

TEST(ReadEnvTest, SenderExists) {
    auto sndr = std::execution::read_env(std::execution::get_stop_token);
    static_assert(std::execution::sender<decltype(sndr)>);
    SUCCEED();
}

TEST(ReadEnvTest, ThrowingQueryCompletesWithError) {
    auto sndr = std::execution::read_env(throwing_query{});
    EXPECT_THROW(std::execution::sync_wait(std::move(sndr)), std::runtime_error);
}

TEST(UponErrorTest, ValuePassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just(10)
              | std::execution::upon_error([&](auto) { fn_called = true; });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(fn_called);
    EXPECT_EQ(std::get<0>(*result), 10);
}

TEST(UponErrorTest, ErrorHandledWithoutThrow) {
    bool fn_called = false;
    auto sndr = std::execution::just_error(42)
              | std::execution::upon_error([&](int) { fn_called = true; });
    EXPECT_NO_THROW(std::execution::sync_wait(std::move(sndr)));
    EXPECT_TRUE(fn_called);
}

TEST(UponErrorTest, ReportsHandledErrorAsValueSignature) {
    auto sndr = std::execution::just_error(42)
              | std::execution::upon_error([](int) { return 3.14; });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(double),
            std::execution::set_error_t(std::exception_ptr)>>);
}

TEST(UponStoppedTest, ValuePassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just(10)
              | std::execution::upon_stopped([&] { fn_called = true; });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(fn_called);
    EXPECT_EQ(std::get<0>(*result), 10);
}

TEST(UponStoppedTest, StoppedHandledWithoutThrow) {
    bool fn_called = false;
    auto sndr = std::execution::just_stopped()
              | std::execution::upon_stopped([&] { fn_called = true; });
    EXPECT_NO_THROW(std::execution::sync_wait(std::move(sndr)));
    EXPECT_TRUE(fn_called);
}

TEST(UponStoppedTest, ReportsHandledStoppedAsValueSignature) {
    auto sndr = std::execution::just_stopped()
              | std::execution::upon_stopped([] { return 7; });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(std::exception_ptr)>>);
}

TEST(LetValueTest, ChainNewSender) {
    auto sndr = std::execution::just(42)
              | std::execution::let_value([](int x) {
                    return std::execution::just(x + 1);
                });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 43);
}

TEST(LetValueTest, ErrorPassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just_error(42)
              | std::execution::let_value([&](auto) {
                    fn_called = true;
                    return std::execution::just(0);
                });
    EXPECT_THROW(std::execution::sync_wait(std::move(sndr)), int);
    EXPECT_FALSE(fn_called);
}

TEST(LetValueTest, ReportsInnerSenderSignatures) {
    auto sndr = std::execution::just(42)
              | std::execution::let_value([](int) {
                    return std::execution::just(3.14);
                });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(double),
            std::execution::set_error_t(std::exception_ptr)>>);
}

TEST(LetErrorTest, HandleError) {
    bool fn_called = false;
    auto sndr = std::execution::just_error(42)
              | std::execution::let_error([&](int) {
                    fn_called = true;
                    return std::execution::just_stopped();
                });
    EXPECT_NO_THROW(std::execution::sync_wait(std::move(sndr)));
    EXPECT_TRUE(fn_called);
}

TEST(LetErrorTest, ValuePassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just(10)
              | std::execution::let_error([&](auto) {
                    fn_called = true;
                    return std::execution::just(0);
                });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(fn_called);
    EXPECT_EQ(std::get<0>(*result), 10);
}

TEST(LetErrorTest, ReportsInnerSenderSignatures) {
    auto sndr = std::execution::just_error(42)
              | std::execution::let_error([](int) {
                    return std::execution::just_stopped();
                });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_stopped_t(),
            std::execution::set_error_t(std::exception_ptr)>>);
}

TEST(LetStoppedTest, HandleStopped) {
    bool fn_called = false;
    auto sndr = std::execution::just_stopped()
              | std::execution::let_stopped([&] {
                    fn_called = true;
                    return std::execution::just_stopped();
                });
    EXPECT_NO_THROW(std::execution::sync_wait(std::move(sndr)));
    EXPECT_TRUE(fn_called);
}

TEST(LetStoppedTest, ReportsInnerSenderSignatures) {
    auto sndr = std::execution::just_stopped()
              | std::execution::let_stopped([] {
                    return std::execution::just(7);
                });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(std::exception_ptr)>>);
}

TEST(StartsOnTest, RunsOnScheduler) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();

    int result = -1;
    std::thread worker([&] { loop.run(); });

    auto sndr = std::execution::starts_on(sch,
        std::execution::just(42) | std::execution::then([&](int x) {
            result = x;
        }));

    std::execution::sync_wait(std::move(sndr));
    loop.finish();
    worker.join();

    EXPECT_EQ(result, 42);
}

TEST(StoppedAsOptionalTest, SenderExists) {
    auto sndr1 = std::execution::stopped_as_optional(std::execution::just_stopped());
    static_assert(std::execution::sender<decltype(sndr1)>);
    auto sndr2 = std::execution::stopped_as_error(std::execution::just_stopped(), 42);
    static_assert(std::execution::sender<decltype(sndr2)>);
    SUCCEED();
}

TEST(StoppedAsOptionalTest, WrapsSingleValueInOptional) {
    auto sndr = std::execution::stopped_as_optional(std::execution::just(42));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::optional<int>),
            std::execution::set_error_t(std::exception_ptr)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::get<0>(*result).has_value());
    EXPECT_EQ(*std::get<0>(*result), 42);
}

TEST(StoppedAsOptionalTest, WrapsMultiValueInOptionalTuple) {
    auto sndr = std::execution::stopped_as_optional(std::execution::just(1, 2));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::optional<std::tuple<int, int>>),
            std::execution::set_error_t(std::exception_ptr)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::get<0>(*result).has_value());
    EXPECT_EQ(std::get<0>(*std::get<0>(*result)), 1);
    EXPECT_EQ(std::get<1>(*std::get<0>(*result)), 2);
}

TEST(StoppedAsOptionalTest, ConvertsStoppedToEmptyOptional) {
    auto sndr = std::execution::stopped_as_optional(std::execution::just_stopped());
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::optional<std::tuple<>>),
            std::execution::set_error_t(std::exception_ptr)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(std::get<0>(*result).has_value());
}

TEST(StoppedAsOptionalTest, ValueConstructionThrowCompletesWithError) {
    auto sndr = std::execution::stopped_as_optional(throwing_value_sender{});

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sndr)), std::runtime_error);
}

TEST(StoppedAsErrorTest, ConvertsStoppedToError) {
    auto sndr = std::execution::stopped_as_error(std::execution::just_stopped(), 42);
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<std::execution::set_error_t(int)>>);

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sndr)), int);
}
