#pragma once

#include <execution>

#include <array>
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace initial_capture_probe {

namespace ex = std::execution;

enum class kind { then, upon_error, upon_stopped, let_value, let_error, let_stopped,
                  bulk, bulk_unchunked, bulk_chunked };
enum class category { lvalue, const_lvalue, rvalue, const_rvalue };

template<kind Kind>
inline constexpr bool is_let = Kind == kind::let_value || Kind == kind::let_error ||
    Kind == kind::let_stopped;

template<kind Kind>
inline constexpr bool is_bulk = Kind == kind::bulk || Kind == kind::bulk_unchunked ||
    Kind == kind::bulk_chunked;

template<kind Kind>
inline constexpr auto adaptor = [] {
    if constexpr (Kind == kind::then) return ex::then;
    else if constexpr (Kind == kind::upon_error) return ex::upon_error;
    else if constexpr (Kind == kind::upon_stopped) return ex::upon_stopped;
    else if constexpr (Kind == kind::let_value) return ex::let_value;
    else if constexpr (Kind == kind::let_error) return ex::let_error;
    else if constexpr (Kind == kind::let_stopped) return ex::let_stopped;
    else if constexpr (Kind == kind::bulk) return ex::bulk;
    else if constexpr (Kind == kind::bulk_unchunked) return ex::bulk_unchunked;
    else return ex::bulk_chunked;
}();

template<kind Kind>
auto source() {
    if constexpr (Kind == kind::upon_error || Kind == kind::let_error) return ex::just_error(7);
    else if constexpr (Kind == kind::upon_stopped || Kind == kind::let_stopped) return ex::just_stopped();
    else return ex::just(7);
}

template<kind Kind>
using source_t = decltype(source<Kind>());

template<kind Kind, class Fn>
concept partial_callable =
    (is_bulk<Kind> && requires(Fn&& fn) { adaptor<Kind>(3, static_cast<Fn&&>(fn)); }) ||
    (!is_bulk<Kind> && requires(Fn&& fn) { adaptor<Kind>(static_cast<Fn&&>(fn)); });

template<kind Kind, class Fn, class S = source_t<Kind>>
concept full_callable =
    (is_bulk<Kind> && requires(S&& s, Fn&& fn) {
        adaptor<Kind>(static_cast<S&&>(s), 3, static_cast<Fn&&>(fn));
    }) ||
    (!is_bulk<Kind> && requires(S&& s, Fn&& fn) {
        adaptor<Kind>(static_cast<S&&>(s), static_cast<Fn&&>(fn));
    });

template<kind Kind, bool Full, class Fn>
    requires (Full ? full_callable<Kind, Fn> : partial_callable<Kind, Fn>)
auto make(Fn&& fn) {
    if constexpr (Full && is_bulk<Kind>) return adaptor<Kind>(source<Kind>(), 3, static_cast<Fn&&>(fn));
    else if constexpr (Full) return adaptor<Kind>(source<Kind>(), static_cast<Fn&&>(fn));
    else if constexpr (is_bulk<Kind>) return adaptor<Kind>(3, static_cast<Fn&&>(fn));
    else return adaptor<Kind>(static_cast<Fn&&>(fn));
}

template<category Category, class T>
decltype(auto) as_category(T& object) noexcept {
    if constexpr (Category == category::lvalue) return (object);
    else if constexpr (Category == category::const_lvalue) return std::as_const(object);
    else if constexpr (Category == category::rvalue) return std::move(object);
    else return std::move(std::as_const(object));
}

template<class Holder>
decltype(auto) captured(Holder& holder) noexcept {
    if constexpr (requires { holder.fn_; }) return (holder.fn_);
    else if constexpr (requires { holder.__fn; }) return (holder.__fn);
    else return (holder.__fn_);
}

template<class Holder>
decltype(auto) captured_source(Holder& holder) noexcept {
    if constexpr (requires { holder.sndr_; }) return (holder.sndr_);
    else return (holder.__sndr);
}

struct capture_state {
    std::array<int, 4> constructions{};
    std::array<bool, 4> throw_on{};
    int calls = 0;
};

struct capture_failure { category selected; };

inline void record(capture_state* state, category selected) {
    if (state) {
        auto index = static_cast<std::size_t>(selected);
        ++state->constructions[index];
        if (state->throw_on[index]) throw capture_failure{selected};
    }
}

template<bool ReturnsSender>
struct callback_body {
    capture_state* state = nullptr;
    int value = 17;

    callback_body() = default;
    explicit callback_body(capture_state& s) noexcept : state(&s) {}

    template<class... Args>
    auto operator()(Args&&...) const noexcept {
        if (state) ++state->calls;
        if constexpr (ReturnsSender) return ex::just(value);
        else return value;
    }
};

template<bool ReturnsSender>
struct tracked_callback : callback_body<ReturnsSender> {
    using base = callback_body<ReturnsSender>;
    using base::base;
    tracked_callback() = default;
    explicit tracked_callback(tracked_callback& other) : base(other) {
        record(this->state, category::lvalue);
    }
    tracked_callback(const tracked_callback& other) : base(other) {
        record(this->state, category::const_lvalue);
    }
    tracked_callback(tracked_callback&& other) : base(other) {
        record(this->state, category::rvalue);
    }
    explicit tracked_callback(const tracked_callback&& other) : base(other) {
        record(this->state, category::const_rvalue);
    }
};

template<bool ReturnsSender>
struct explicit_copy_callback : callback_body<ReturnsSender> {
    using base = callback_body<ReturnsSender>;
    using base::base;
    explicit_copy_callback() = default;
    explicit explicit_copy_callback(const explicit_copy_callback& other) : base(other) {
        record(this->state, category::const_lvalue);
    }
    explicit_copy_callback(explicit_copy_callback&& other) : base(other) {
        record(this->state, category::rvalue);
    }
};

template<bool ReturnsSender>
struct move_only_callback : callback_body<ReturnsSender> {
    using callback_body<ReturnsSender>::callback_body;
    move_only_callback() = default;
    move_only_callback(move_only_callback&&) = default;
    move_only_callback(const move_only_callback&) = delete;
};

template<bool ReturnsSender>
struct deleted_mutable_copy : callback_body<ReturnsSender> {
    deleted_mutable_copy() = default;
    deleted_mutable_copy(deleted_mutable_copy&&) = default;
    deleted_mutable_copy(const deleted_mutable_copy&) = default;
    deleted_mutable_copy(deleted_mutable_copy&) = delete;
};

template<bool ReturnsSender>
struct deleted_const_move : callback_body<ReturnsSender> {
    deleted_const_move() = default;
    deleted_const_move(deleted_const_move&&) = default;
    deleted_const_move(const deleted_const_move&) = default;
    deleted_const_move(const deleted_const_move&&) = delete;
};

struct tracked_source {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    capture_state* state;
    int value = 7;

    explicit tracked_source(capture_state& s) noexcept : state(&s) {}
    explicit tracked_source(tracked_source& other) : state(other.state), value(other.value) {
        record(state, category::lvalue);
    }
    explicit tracked_source(const tracked_source& other) : state(other.state), value(other.value) {
        record(state, category::const_lvalue);
    }
    tracked_source(tracked_source&& other) : state(other.state), value(other.value) {
        record(state, category::rvalue);
    }
    explicit tracked_source(const tracked_source&& other) : state(other.state), value(other.value) {
        record(state, category::const_rvalue);
    }

    ex::empty_env get_env() const noexcept { return {}; }

    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_t;
        R receiver;
        int value;
        void start() & noexcept { ex::set_value(std::move(receiver), value); }
    };

    template<ex::receiver R>
    auto connect(R receiver) && {
        return operation<R>{std::move(receiver), value};
    }
};

} // namespace initial_capture_probe
