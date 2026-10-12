#include <gtest/gtest.h>
#include <execution>
#include <exception>
#include <coroutine>
#include <type_traits>
#include <variant>
#include <tuple>
#include <utility>

namespace {
namespace ex = std::execution;

struct borrowed_env {
    std::inplace_stop_token token;
    explicit borrowed_env(std::inplace_stop_token t) : token(t) {}
    borrowed_env(const borrowed_env&) = delete;
    auto query(ex::get_stop_token_t) const noexcept { return token; }
};

struct owned_env {
    std::inplace_stop_token token;
    auto query(ex::get_stop_token_t) const noexcept { return token; }
};

template<class Env>
struct environment_shape {};

struct shape_sender {
    using sender_concept = ex::sender_t;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(environment_shape<Env>)> { return {}; }
    auto get_env() const noexcept -> ex::empty_env { return {}; }
    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_t;
        R receiver;
        void start() & noexcept {
            using env = std::remove_cvref_t<ex::env_of_t<R>>;
            ex::set_value(std::move(receiver), environment_shape<env>{});
        }
    };
    template<class R>
    auto connect(R receiver) const -> operation<R> { return {std::move(receiver)}; }
};

template<class Value>
struct exact_shape_receiver {
    using receiver_concept = ex::receiver_t;
    const borrowed_env* env;
    bool* completed;
    auto get_env() const noexcept -> const borrowed_env& { return *env; }
    void set_value(Value) && noexcept { *completed = true; }
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

template<class S>
void check_advertised_shape(S sender) {
    std::inplace_stop_source source;
    borrowed_env env{source.get_token()};
    using value = std::tuple_element_t<0, std::variant_alternative_t<0,
        ex::value_types_of_t<S, const borrowed_env&, std::tuple, std::variant>>>;
    bool completed = false;
    auto op = ex::connect(std::move(sender), exact_shape_receiver<value>{&env, &completed});
    ex::start(op);
    EXPECT_TRUE(completed);
}

template<class Env, bool Borrow>
struct env_receiver {
    using receiver_concept = ex::receiver_t;
    const Env* env;
    int* result;
    decltype(auto) get_env() const noexcept {
        if constexpr (Borrow) return (*env);
        else return Env{*env};
    }
    void set_value(int value) && noexcept { *result = value; }
    void set_error(std::exception_ptr) && noexcept { *result = -1; }
    void set_stopped() && noexcept { *result = -2; }
};

template<class S>
void check_environment(S sender, const borrowed_env& env) {
    int result = 0;
    auto op = ex::connect(std::move(sender), env_receiver<borrowed_env, true>{&env, &result});
    ex::start(op);
    EXPECT_EQ(result, 42);
}

template<class S>
void check_environment(S sender, const owned_env& env) {
    int result = 0;
    auto receiver = env_receiver<owned_env, false>{&env, &result};
    auto op = ex::connect(std::move(sender), std::move(receiver));
    ex::start(op);
    EXPECT_EQ(result, 42);
}

auto inspect_environment(std::inplace_stop_token expected) {
    return ex::read_env(ex::get_stop_token) | ex::then([expected](auto token) {
        return token == expected ? 42 : -3;
    });
}

template<bool Borrow>
struct env_task {
    struct promise_type : ex::with_awaitable_senders<promise_type> {
        std::conditional_t<Borrow, borrowed_env, owned_env> env;
        promise_type(std::inplace_stop_token token, int*) : env(token) {}
        env_task get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
        decltype(auto) get_env() const noexcept {
            if constexpr (Borrow) return (env);
            else return owned_env{env};
        }
    };
};

template<bool Borrow>
env_task<Borrow> await_environment(std::inplace_stop_token token, int* result) {
    *result = co_await inspect_environment(token);
}
}

TEST(ExecutionBorrowedEnvTest, LetAdaptorsPreserveNoncopyableEnvironment) {
    std::inplace_stop_source source;
    borrowed_env env{source.get_token()};
    auto inner = [&] { return inspect_environment(source.get_token()); };
    check_environment(ex::let_value(ex::just(), inner), env);
    check_environment(ex::let_error(ex::just_error(1), [&](int&) { return inner(); }), env);
    check_environment(ex::let_stopped(ex::just_stopped(), inner), env);
    owned_env owned{source.get_token()};
    check_environment(ex::let_value(ex::just(), inner), owned);
    check_environment(ex::let_error(ex::just_error(1), [&](int&) { return inner(); }), owned);
    check_environment(ex::let_stopped(ex::just_stopped(), inner), owned);
}

TEST(ExecutionBorrowedEnvTest, LetAdvertisesRuntimeEnvironmentShape) {
    check_advertised_shape(ex::let_value(ex::just(), [] { return shape_sender{}; }));
    check_advertised_shape(ex::let_error(ex::just_error(1), [](int&) { return shape_sender{}; }));
    check_advertised_shape(ex::let_stopped(ex::just_stopped(), [] { return shape_sender{}; }));
}

TEST(ExecutionBorrowedEnvTest, StartsOnBorrowsReferencesAndOwnsValues) {
    std::inplace_stop_source source;
    borrowed_env borrowed{source.get_token()};
    owned_env owned{source.get_token()};
    check_environment(ex::starts_on(ex::inline_scheduler{},
        inspect_environment(source.get_token())), borrowed);
    check_environment(ex::starts_on(ex::inline_scheduler{},
        inspect_environment(source.get_token())), owned);
}

TEST(ExecutionBorrowedEnvTest, StartsOnAdvertisesRuntimeEnvironmentShape) {
    check_advertised_shape(ex::starts_on(ex::inline_scheduler{}, shape_sender{}));
}

TEST(ExecutionBorrowedEnvTest, WriteEnvBorrowsReferencesAndOwnsValues) {
    std::inplace_stop_source source;
    borrowed_env borrowed{source.get_token()};
    owned_env owned{source.get_token()};
    check_environment(ex::write_env(inspect_environment(source.get_token()), ex::empty_env{}), borrowed);
    check_environment(ex::write_env(inspect_environment(source.get_token()), ex::empty_env{}), owned);
    std::inplace_stop_source overlay;
    check_environment(ex::write_env(inspect_environment(overlay.get_token()),
        owned_env{overlay.get_token()}), borrowed);
    check_environment(ex::write_env(inspect_environment(overlay.get_token()),
        owned_env{overlay.get_token()}), owned);
}

TEST(ExecutionBorrowedEnvTest, PromiseBorrowsReferencesAndOwnsValues) {
    std::inplace_stop_source source;
    int result = 0;
    await_environment<true>(source.get_token(), &result);
    EXPECT_EQ(result, 42);
    result = 0;
    await_environment<false>(source.get_token(), &result);
    EXPECT_EQ(result, 42);
}
