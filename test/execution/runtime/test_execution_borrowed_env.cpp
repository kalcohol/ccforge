#include <gtest/gtest.h>
#include <execution>
#include <exception>
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

TEST(ExecutionBorrowedEnvTest, StartsOnBorrowsReferencesAndOwnsValues) {
    std::inplace_stop_source source;
    borrowed_env borrowed{source.get_token()};
    owned_env owned{source.get_token()};
    check_environment(ex::starts_on(ex::inline_scheduler{},
        inspect_environment(source.get_token())), borrowed);
    check_environment(ex::starts_on(ex::inline_scheduler{},
        inspect_environment(source.get_token())), owned);
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
