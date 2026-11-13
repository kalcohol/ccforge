#pragma once

#include <execution>

#include <array>
#include <concepts>
#include <type_traits>
#include <utility>

namespace public_closure_probe {
namespace ex = std::execution;

template<class Left, class Right>
concept pipeable = requires(Left&& left, Right&& right) {
    static_cast<Left&&>(left) | static_cast<Right&&>(right);
};

struct literal_sender {
    using sender_concept = ex::sender_tag;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    int value;
    constexpr ex::env<> get_env() const noexcept { return {}; }
};

struct identity : ex::sender_adaptor_closure<identity> {
    template<ex::sender S>
    constexpr decltype(auto) operator()(S&& source) const noexcept {
        return static_cast<S&&>(source);
    }
};

struct rvalue_only : ex::sender_adaptor_closure<rvalue_only> {
    rvalue_only() = default;
    rvalue_only(rvalue_only&&) = default;
    rvalue_only(const rvalue_only&) = delete;

    template<ex::sender S>
    constexpr decltype(auto) operator()(S&& source) && noexcept {
        return static_cast<S&&>(source);
    }
};

enum class category { lvalue, const_lvalue, rvalue, const_rvalue };
struct call_state {
    std::array<int, 2> order{};
    std::array<category, 2> categories{};
    int count = 0;
};

template<bool Nothrow>
struct traced : ex::sender_adaptor_closure<traced<Nothrow>> {
    call_state* state;
    int index;

    constexpr void record(category selected) const noexcept {
        state->order[state->count] = index;
        state->categories[state->count++] = selected;
    }

    template<ex::sender S>
    constexpr decltype(auto) operator()(S&& source) & noexcept(Nothrow) {
        record(category::lvalue);
        return static_cast<S&&>(source);
    }
    template<ex::sender S>
    constexpr decltype(auto) operator()(S&& source) const& noexcept(Nothrow) {
        record(category::const_lvalue);
        return static_cast<S&&>(source);
    }
    template<ex::sender S>
    constexpr decltype(auto) operator()(S&& source) && noexcept(Nothrow) {
        record(category::rvalue);
        return static_cast<S&&>(source);
    }
    template<ex::sender S>
    constexpr decltype(auto) operator()(S&& source) const&& noexcept(Nothrow) {
        record(category::const_rvalue);
        return static_cast<S&&>(source);
    }
};

template<category Category, class T>
constexpr decltype(auto) as_category(T& object) noexcept {
    if constexpr (Category == category::lvalue) return (object);
    else if constexpr (Category == category::const_lvalue) return std::as_const(object);
    else if constexpr (Category == category::rvalue) return std::move(object);
    else return std::move(std::as_const(object));
}

struct scale : ex::sender_adaptor_closure<scale> {
    int factor = 2;
    template<ex::sender S>
    auto operator()(S&& source) const {
        return ex::then(static_cast<S&&>(source),
            [factor = factor](int value) noexcept { return value * factor; });
    }
};

} // namespace public_closure_probe
