#pragma once

#include <execution>
#include <optional>
#include <utility>

namespace stopped_optional_probe {

namespace ex = std::execution;

template<class CS>
struct signature_sender {
    using sender_concept = ex::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> CS { return {}; }

    auto get_env() const noexcept -> ex::empty_env { return {}; }
};

template<class CS>
struct environment {
    using optional_completions = CS;
};

using value_env = environment<ex::completion_signatures<
    ex::set_value_t(int), ex::set_stopped_t()>>;
using void_env = environment<ex::completion_signatures<
    ex::set_value_t(), ex::set_stopped_t()>>;
using no_value_env = environment<ex::completion_signatures<ex::set_stopped_t()>>;
using multi_env = environment<ex::completion_signatures<
    ex::set_value_t(int), ex::set_value_t(const int&), ex::set_stopped_t()>>;

struct dependent_sender {
    using sender_concept = ex::sender_t;

    template<class Self, class Env>
        requires requires { typename Env::optional_completions; }
    static auto get_completion_signatures() noexcept
        -> typename Env::optional_completions { return {}; }

    auto get_env() const noexcept -> ex::empty_env { return {}; }

    template<ex::receiver R>
    struct operation : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        R receiver;

        explicit operation(R r) : receiver(std::move(r)) {}
        void start() & noexcept { ex::set_stopped(std::move(receiver)); }
    };

    template<ex::receiver R>
    auto connect(R r) const -> operation<R> { return operation<R>{std::move(r)}; }
};

template<class Env>
struct receiver {
    using receiver_concept = ex::receiver_t;

    template<class T>
    void set_value(T&&) && noexcept {}
    template<class E>
    void set_error(E&&) && noexcept {}
    void set_stopped() && noexcept {}
    auto get_env() const noexcept -> Env { return {}; }
};

} // namespace stopped_optional_probe
