#include <gtest/gtest.h>
#include <execution>
#include <concepts>
#include <exception>
#include <stdexcept>
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

struct start_scheduler_receiver {
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
            std::execution::make_prop(std::execution::get_start_scheduler_t{}, expected));
    }
};

struct delegation_scheduler_receiver {
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
            std::execution::make_prop(std::execution::get_delegation_scheduler_t{}, expected));
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

struct member_forwarding_query {
    constexpr bool query(std::forwarding_query_t) const noexcept {
        return true;
    }
};

struct tag_forwarding_query {
    friend constexpr bool tag_invoke(std::forwarding_query_t, tag_forwarding_query) noexcept {
        return true;
    }
};

struct await_completion_adaptor_env {
    int value = 0;

    friend int tag_invoke(
        std::execution::get_await_completion_adaptor_t,
        const await_completion_adaptor_env& self) noexcept {
        return self.value;
    }
};

struct member_stop_query_env {
    std::inplace_stop_token token;

    auto query(std::execution::get_stop_token_t) const noexcept
        -> std::inplace_stop_token {
        return token;
    }
};

struct overlay_value_query_t {
    template<class Env>
        requires std::execution::__forge_env_detail::__queryable<
            overlay_value_query_t,
            Env>
    decltype(auto) operator()(Env&& env) const noexcept(
        std::execution::__forge_env_detail::__nothrow_query<
            overlay_value_query_t,
            Env>) {
        return std::execution::__forge_env_detail::__query(
            *this,
            static_cast<Env&&>(env));
    }
};

inline constexpr overlay_value_query_t overlay_value_query{};

struct throwing_copy_env {
    int value = 0;

    throwing_copy_env() = default;
    explicit throwing_copy_env(int initial) noexcept : value(initial) {}
    throwing_copy_env(throwing_copy_env&&) noexcept = default;
    throwing_copy_env& operator=(throwing_copy_env&&) noexcept = default;

    throwing_copy_env(const throwing_copy_env&) {
        throw std::runtime_error("overlay copied");
    }

    throwing_copy_env& operator=(const throwing_copy_env&) = delete;

    friend int tag_invoke(
        overlay_value_query_t,
        const throwing_copy_env& self) noexcept {
        return self.value;
    }
};

} // namespace

static_assert(std::forwarding_query(std::execution::get_scheduler));
static_assert(std::forwarding_query(std::execution::get_start_scheduler));
static_assert(std::forwarding_query(std::execution::get_delegation_scheduler));
static_assert(std::forwarding_query(std::execution::get_stop_token));
static_assert(std::forwarding_query(std::execution::get_allocator));
static_assert(std::forwarding_query(std::execution::get_completion_scheduler<std::execution::set_value_t>));
static_assert(std::forwarding_query(std::execution::get_domain));
static_assert(std::forwarding_query(std::execution::get_completion_domain<std::execution::set_value_t>));
static_assert(std::forwarding_query(std::execution::get_await_completion_adaptor));
static_assert(std::forwarding_query(member_forwarding_query{}));
static_assert(std::forwarding_query(tag_forwarding_query{}));
static_assert(!std::forwarding_query(std::execution::set_value));

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
            std::execution::set_value_t(std::execution::inline_scheduler)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == injected);
}

TEST(WriteEnvTest, DoesNotCopyOverlayWhileQueryingNestedAdaptorEnv) {
    auto sender = std::execution::write_env(
        std::execution::read_env(overlay_value_query)
            | std::execution::then([](int value) noexcept { return value; }),
        throwing_copy_env{42});

    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
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

TEST(WriteEnvTest, ReadsInjectedStartScheduler) {
    std::execution::inline_scheduler injected;
    auto env = std::execution::make_env(
        std::execution::make_prop(std::execution::get_start_scheduler_t{}, injected));

    auto result = std::execution::sync_wait(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_start_scheduler),
            env));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == injected);
}

TEST(WriteEnvTest, ReadsInjectedDelegationScheduler) {
    std::execution::inline_scheduler injected;
    auto env = std::execution::make_env(
        std::execution::make_prop(std::execution::get_delegation_scheduler_t{}, injected));

    auto result = std::execution::sync_wait(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_delegation_scheduler),
            env));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result) == injected);
}

TEST(SyncWaitEnvTest, ExposesTheSameRunLoopSchedulerForAllRoles) {
    auto result = std::execution::sync_wait(
        std::execution::when_all(
            std::execution::read_env(std::execution::get_scheduler),
            std::execution::read_env(std::execution::get_start_scheduler),
            std::execution::read_env(std::execution::get_delegation_scheduler))
        | std::execution::then([](auto scheduler, auto start, auto delegation) {
              return scheduler == start && scheduler == delegation;
          }));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
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

TEST(WriteEnvTest, FallsBackToReceiverStartSchedulerForMissingQuery) {
    std::execution::inline_scheduler downstream;
    bool got_expected = false;
    bool completed = false;

    auto op = std::execution::connect(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_start_scheduler),
            std::execution::empty_env{}),
        start_scheduler_receiver{downstream, &got_expected, &completed});

    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(got_expected);
}

TEST(WriteEnvTest, FallsBackToReceiverDelegationSchedulerForMissingQuery) {
    std::execution::inline_scheduler downstream;
    bool got_expected = false;
    bool completed = false;

    auto op = std::execution::connect(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_delegation_scheduler),
            std::execution::empty_env{}),
        delegation_scheduler_receiver{downstream, &got_expected, &completed});

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

TEST(WriteEnvTest, ReadsMemberQueriedStopTokenFromInjectedEnv) {
    std::inplace_stop_source source;
    EXPECT_TRUE(source.request_stop());

    auto result = std::execution::sync_wait(std::execution::write_env(
        std::execution::read_env(std::execution::get_stop_token),
        member_stop_query_env{source.get_token()}));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result).stop_requested());
}

TEST(EnvTest, DuplicateQueriesUseTheLeftmostEnvironment) {
    std::inplace_stop_source first;
    std::inplace_stop_source second;
    auto env = std::execution::make_env(
        std::execution::make_prop(
            std::execution::get_stop_token_t{}, first.get_token()),
        std::execution::make_prop(
            std::execution::get_stop_token_t{}, second.get_token()));

    auto token = std::execution::get_stop_token(env);
    EXPECT_TRUE(token.stop_possible());
    EXPECT_FALSE(token.stop_requested());

    second.request_stop();
    EXPECT_FALSE(token.stop_requested());

    first.request_stop();
    EXPECT_TRUE(token.stop_requested());
}

TEST(QueryCpoTest, ReadsAwaitCompletionAdaptorThroughBackportQueryModel) {
    await_completion_adaptor_env env{42};

    EXPECT_EQ(std::execution::get_await_completion_adaptor(env), 42);
}

TEST(UnstoppableTest, OverridesReceiverStopTokenWithNeverStopToken) {
    std::inplace_stop_source source;
    source.request_stop();
    auto env = std::execution::make_env(
        std::execution::make_prop(
            std::execution::get_stop_token_t{}, source.get_token()));

    auto sndr = std::execution::write_env(
        std::execution::unstoppable(
            std::execution::read_env(std::execution::get_stop_token)),
        env);

    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::never_stop_token)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    auto token = std::get<0>(*result);
    static_assert(std::unstoppable_token<decltype(token)>);
    EXPECT_FALSE(token.stop_possible());
    EXPECT_FALSE(token.stop_requested());
}

TEST(UnstoppableTest, PipeFormOverridesStopToken) {
    std::inplace_stop_source source;
    auto env = std::execution::make_env(
        std::execution::make_prop(
            std::execution::get_stop_token_t{}, source.get_token()));

    auto result = std::execution::sync_wait(
        std::execution::write_env(
            std::execution::read_env(std::execution::get_stop_token)
                | std::execution::unstoppable(),
            env));

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(std::get<0>(*result).stop_possible());
}
