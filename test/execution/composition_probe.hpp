#pragma once

#include <execution>

#include <array>
#include <concepts>
#include <type_traits>
#include <utility>

namespace composition_probe {

namespace ex = std::execution;
enum class category { lvalue, const_lvalue, rvalue, const_rvalue };

template<category Category, class T>
decltype(auto) as_category(T& object) noexcept {
    if constexpr (Category == category::lvalue) return (object);
    else if constexpr (Category == category::const_lvalue) return std::as_const(object);
    else if constexpr (Category == category::rvalue) return std::move(object);
    else return std::move(std::as_const(object));
}

struct value_callback {
    int delta = 0;
    int operator()(int value) const noexcept { return value + delta; }
};

using source_t = decltype(ex::just(7));
using value_closure = decltype(ex::then(value_callback{}));

template<class First, class Second>
concept composable = requires(First&& first, Second&& second) {
    static_cast<First&&>(first) | static_cast<Second&&>(second);
};

template<class C, class S = source_t>
concept applicable = std::is_invocable_v<C, S> && requires(C&& closure, S&& source) {
    { static_cast<C&&>(closure)(static_cast<S&&>(source)) } -> ex::sender;
    { static_cast<S&&>(source) | static_cast<C&&>(closure) } -> ex::sender;
};

template<class C, class S = source_t>
concept rejected = !std::is_invocable_v<C, S> &&
    !requires(C&& closure, S&& source) { static_cast<C&&>(closure)(static_cast<S&&>(source)); } &&
    !requires(C&& closure, S&& source) { static_cast<S&&>(source) | static_cast<C&&>(closure); };

template<bool ReturnsSender>
struct move_only_callback {
    move_only_callback() = default;
    move_only_callback(move_only_callback&&) = default;
    move_only_callback(const move_only_callback&) = delete;
    template<class... Args>
    auto operator()(Args&&...) const noexcept {
        if constexpr (ReturnsSender) return ex::just(42);
        else return 42;
    }
};

struct capture_state {
    std::array<int, 4> constructions{};
    std::array<bool, 4> throw_on{};
    int calls = 0;
    int alive = 0;
};

struct capture_failure { category selected; };

struct capture_handler {
    capture_state* state;

    explicit capture_handler(capture_state& s) noexcept : state(&s) { ++state->alive; }
    explicit capture_handler(capture_handler& other) : state(other.state) { record(category::lvalue); }
    capture_handler(const capture_handler& other) : state(other.state) { record(category::const_lvalue); }
    capture_handler(capture_handler&& other) : state(other.state) { record(category::rvalue); }
    explicit capture_handler(const capture_handler&& other) : state(other.state) { record(category::const_rvalue); }
    ~capture_handler() { --state->alive; }

    void record(category selected) {
        auto index = static_cast<std::size_t>(selected);
        ++state->constructions[index];
        if (state->throw_on[index]) throw capture_failure{selected};
        ++state->alive;
    }

    template<ex::sender S>
    auto operator()(S&& source) const noexcept(std::is_nothrow_constructible_v<std::decay_t<S>, S>) {
        ++state->calls;
        return std::decay_t<S>(static_cast<S&&>(source));
    }
};

struct literal_source {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    int value;
    constexpr ex::empty_env get_env() const noexcept { return {}; }
};

struct forwarding_identity {
    template<ex::sender S>
    constexpr decltype(auto) operator()(S&& source) const noexcept {
        return static_cast<S&&>(source);
    }
};

struct call_failure {};

struct call_state {
    std::array<int, 2> order{};
    std::array<category, 2> categories{};
    int count = 0;
};

template<bool Nothrow>
struct category_handler {
    call_state* state;
    int identity;
    bool* throw_now = nullptr;

    void record(category selected) const noexcept(Nothrow) {
        state->order[state->count] = identity;
        state->categories[state->count++] = selected;
        if constexpr (!Nothrow) {
            if (throw_now && *throw_now) throw call_failure{};
        }
    }

    template<ex::sender S>
    decltype(auto) operator()(S&& source) & noexcept(Nothrow) {
        record(category::lvalue);
        return static_cast<S&&>(source);
    }
    template<ex::sender S>
    decltype(auto) operator()(S&& source) const& noexcept(Nothrow) {
        record(category::const_lvalue);
        return static_cast<S&&>(source);
    }
    template<ex::sender S>
    decltype(auto) operator()(S&& source) && noexcept(Nothrow) {
        record(category::rvalue);
        return static_cast<S&&>(source);
    }
    template<ex::sender S>
    decltype(auto) operator()(S&& source) const&& noexcept(Nothrow) {
        record(category::const_rvalue);
        return static_cast<S&&>(source);
    }
};

struct rvalue_only_handler {
    template<ex::sender S>
    constexpr decltype(auto) operator()(S&& source) && noexcept {
        return static_cast<S&&>(source);
    }
    template<class S> void operator()(S&&) & = delete;
    template<class S> void operator()(S&&) const& = delete;
    template<class S> void operator()(S&&) const&& = delete;
};

// Core probes are private implementation tests, not public closure opt-ins.
template<class First, class Second>
using core = ex::__forge_adaptor::__composed<First, Second>;

} // namespace composition_probe
