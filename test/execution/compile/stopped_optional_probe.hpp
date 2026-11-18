#pragma once

#include <forge/execution_extensions.hpp>
#include <execution>
#include <optional>
#include <tuple>
#include <utility>

namespace stopped_optional_probe {

namespace ex = std::execution;

template<class CS>
struct signature_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> CS { return {}; }

    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

using single_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(int), ex::set_error_t(long), ex::set_stopped_t()>>;
using tuple_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(int, double), ex::set_stopped_t()>>;
using empty_tuple_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(std::tuple<>), ex::set_stopped_t()>>;
using zero_sender = signature_sender<ex::completion_signatures<>>;
using void_sender = signature_sender<ex::completion_signatures<ex::set_value_t()>>;
using error_sender = signature_sender<ex::completion_signatures<ex::set_error_t(int)>>;
using stopped_sender = signature_sender<ex::completion_signatures<ex::set_stopped_t()>>;
using multi_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(int), ex::set_value_t(double)>>;
using same_decay_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(int), ex::set_value_t(const int&)>>;
using mixed_void_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(), ex::set_value_t(int)>>;

struct completion_query : std::forwarding_query_t {};
struct local_query {};

template<class CS>
struct environment {
    auto query(completion_query) const noexcept -> CS { return {}; }
    int query(local_query) const noexcept { return 42; }
};

using value_env = environment<ex::completion_signatures<
    ex::set_value_t(int), ex::set_stopped_t()>>;
using void_env = environment<ex::completion_signatures<
    ex::set_value_t(), ex::set_stopped_t()>>;
using no_value_env = environment<ex::completion_signatures<ex::set_stopped_t()>>;
using multi_env = environment<ex::completion_signatures<
    ex::set_value_t(int), ex::set_value_t(const int&), ex::set_stopped_t()>>;

struct dependent_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class Env>
        requires requires(const Env& env) { env.query(completion_query{}); }
    static auto get_completion_signatures() noexcept
        -> decltype(std::declval<const Env&>().query(completion_query{})) { return {}; }

    auto get_env() const noexcept -> forge::empty_env { return {}; }

    template<ex::receiver R>
    struct operation : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;

        explicit operation(R r) : receiver(std::move(r)) {}
        void start() & noexcept { ex::set_stopped(std::move(receiver)); }
    };

    template<ex::receiver R>
    auto connect(R r) const -> operation<R> { return operation<R>{std::move(r)}; }
};

template<class Env>
struct receiver {
    using receiver_concept = ex::receiver_tag;

    template<class T>
    void set_value(T&&) && noexcept {}
    template<class E>
    void set_error(E&&) && noexcept {}
    void set_stopped() && noexcept {}
    auto get_env() const noexcept -> Env { return {}; }
};

struct scheduler_env {
    auto query(ex::get_scheduler_t) const noexcept -> ex::inline_scheduler { return {}; }
};

struct empty_void_query {
    template<class Env>
    auto operator()(const Env&) const noexcept {
        if constexpr (requires(const Env& env) { env.query(completion_query{}); }) {
            return 42;
        }
    }
};

namespace runtime {

struct optional_query : std::forwarding_query_t {};
struct local_optional_query {};
struct optional_env {
    int query(optional_query) const noexcept { return 42; }
    int query(local_optional_query) const noexcept { return 99; }
};

struct empty_void_query {
    template<class Env>
    auto operator()(const Env& env) const noexcept {
        if constexpr (requires { env.query(optional_query{}); }) {
            return env.query(optional_query{});
        }
    }
};

struct observed_result {
    int values = 0;
    int errors = 0;
    int value = 0;
    bool engaged = false;
};

struct optional_receiver {
    using receiver_concept = ex::receiver_tag;
    observed_result* result;

    void set_value(std::optional<int>&& value) && noexcept {
        ++result->values;
        result->engaged = value.has_value();
        if (value) {
            result->value = *value;
        }
    }

    void set_error(int value) && noexcept {
        ++result->errors;
        result->value = value;
    }

    auto get_env() const noexcept -> optional_env { return {}; }
};

} // namespace runtime

} // namespace stopped_optional_probe
