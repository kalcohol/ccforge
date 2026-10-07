#pragma once

#include <execution>

#include <array>
#include <concepts>
#include <type_traits>
#include <utility>

namespace associate_closure_probe {

namespace ex = std::execution;
enum class category { lvalue, const_lvalue, rvalue, const_rvalue };

template<category Category, class T>
decltype(auto) as_category(T& object) noexcept {
    if constexpr (Category == category::lvalue) return (object);
    else if constexpr (Category == category::const_lvalue) return std::as_const(object);
    else if constexpr (Category == category::rvalue) return std::move(object);
    else return std::move(std::as_const(object));
}

struct token_state {
    std::array<int, 4> constructions{};
    int wraps = 0;
    int attempts = 0;
    bool throw_wrap = false;
    bool throw_associate = false;
};

struct wrap_failure {};
struct association_failure {};

struct tracked_token {
    ex::simple_counting_scope::scope_token inner;
    token_state* state;

    tracked_token(ex::simple_counting_scope::scope_token token, token_state& s) noexcept
        : inner(std::move(token)), state(&s) {}
    explicit tracked_token(tracked_token& other) noexcept
        : inner(other.inner), state(other.state) { ++state->constructions[0]; }
    tracked_token(const tracked_token& other) noexcept
        : inner(other.inner), state(other.state) { ++state->constructions[1]; }
    tracked_token(tracked_token&& other) noexcept
        : inner(std::move(other.inner)), state(other.state) { ++state->constructions[2]; }
    explicit tracked_token(const tracked_token&& other) noexcept
        : inner(other.inner), state(other.state) { ++state->constructions[3]; }
    tracked_token& operator=(const tracked_token&) = default;
    tracked_token& operator=(tracked_token&&) = default;

    auto try_associate() const {
        ++state->attempts;
        if (state->throw_associate) throw association_failure{};
        return inner.try_associate();
    }

    template<ex::sender S>
    auto wrap(S&& sender) const {
        ++state->wraps;
        if (state->throw_wrap) throw wrap_failure{};
        return inner.wrap(static_cast<S&&>(sender));
    }
};

static_assert(ex::scope_token<tracked_token>);
static_assert(std::is_nothrow_copy_constructible_v<tracked_token>);
static_assert(std::is_nothrow_move_constructible_v<tracked_token>);

template<category Category, bool Pipe, class Closure, class S>
auto apply(Closure& closure, S&& sender) {
    if constexpr (Pipe) return static_cast<S&&>(sender) | as_category<Category>(closure);
    else return as_category<Category>(closure)(static_cast<S&&>(sender));
}

} // namespace associate_closure_probe
