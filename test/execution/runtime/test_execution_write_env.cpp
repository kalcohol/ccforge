#include <gtest/gtest.h>
#include <execution>
#include <exception>
#include <tuple>
#include <type_traits>

namespace {

struct scheduler_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::execution::inline_scheduler expected;
    bool* got_expected;
    bool* completed;

    void set_value(std::execution::inline_scheduler sch) && noexcept {
        *got_expected = (sch == expected);
        *completed = true;
    }

    void set_error(std::exception_ptr) && noexcept {
        *completed = false;
    }

    void set_stopped() && noexcept {
        *completed = false;
    }

    auto get_env() const noexcept {
        return std::execution::make_env(
            std::execution::make_prop(std::execution::get_scheduler_t{}, expected));
    }
};

struct stop_token_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::inplace_stop_source* source;
    bool* stop_possible;
    bool* completed;

    void set_value(std::inplace_stop_token token) && noexcept {
        *stop_possible = token.stop_possible();
        *completed = true;
    }

    void set_error(std::exception_ptr) && noexcept {
        *completed = false;
    }

    void set_stopped() && noexcept {
        *completed = false;
    }

    auto get_env() const noexcept {
        return std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_stop_token_t{}, source->get_token()));
    }
};

} // namespace

TEST(WriteEnvTest, InjectedEnvOverridesReceiverEnv) {
    std::execution::inline_scheduler injected;
    auto env = std::execution::make_env(
        std::execution::make_prop(std::execution::get_scheduler_t{}, injected));

    auto sndr = std::execution::write_env(
        std::execution::read_env(std::execution::get_scheduler), env);
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::execution::inline_scheduler),
            std::execution::set_error_t(std::exception_ptr)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == injected);
}

TEST(WriteEnvTest, PipeFormInjectsEnv) {
    std::execution::inline_scheduler injected;
    auto env = std::execution::make_env(
        std::execution::make_prop(std::execution::get_scheduler_t{}, injected));

    auto result = std::execution::sync_wait(
        std::execution::read_env(std::execution::get_scheduler)
            | std::execution::write_env(env));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == injected);
}

TEST(WriteEnvTest, FallsBackToReceiverEnvForMissingQuery) {
    std::execution::inline_scheduler downstream;
    bool got_expected = false;
    bool completed = false;

    auto op = std::execution::connect(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_scheduler),
            std::execution::empty_env{}),
        scheduler_receiver{downstream, &got_expected, &completed});

    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(got_expected);
}

TEST(WriteEnvTest, PreservesReceiverStopTokenWhenNotOverridden) {
    std::inplace_stop_source source;
    bool stop_possible = false;
    bool completed = false;

    auto op = std::execution::connect(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_stop_token),
            std::execution::empty_env{}),
        stop_token_receiver{&source, &stop_possible, &completed});

    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(stop_possible);
}
