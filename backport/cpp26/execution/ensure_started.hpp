// MIT License
//
// Copyright (c) 2026 CC Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "concepts.hpp"
#include "detail/op_storage.hpp"
#include "env.hpp"

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace std::execution {

namespace __forge_ensure_started {

template<class Tuple>
struct __value_result {
    Tuple __values;
};

template<class Error>
struct __error_result {
    Error __error;
};

struct __stopped_result {};

template<class Sig>
struct __result_alt;

template<class... Vs>
struct __result_alt<set_value_t(Vs...)> {
    using type = __value_result<std::tuple<std::decay_t<Vs>...>>;
};

template<class Error>
struct __result_alt<set_error_t(Error)> {
    using type = __error_result<std::decay_t<Error>>;
};

template<>
struct __result_alt<set_stopped_t()> {
    using type = __stopped_result;
};

template<class Sig>
using __result_alt_t = typename __result_alt<Sig>::type;

template<class CS>
struct __result_variant;

template<class... Sigs>
struct __result_variant<completion_signatures<Sigs...>> {
private:
    using completion_list = __forge_meta::list_push_unique_all_t<
        __forge_meta::type_list<std::monostate>,
        __result_alt_t<Sigs>...>;
    using with_exception = __forge_meta::list_push_unique_t<
        completion_list,
        __error_result<std::exception_ptr>>;

public:
    using type = __forge_meta::list_to_variant_t<std::variant, with_exception>;
};

template<class CS>
using __result_variant_t = typename __result_variant<CS>::type;

template<class CS>
using __output_completion_signatures_t = __forge_meta::__concat_unique_cs_t<
    CS,
    completion_signatures<set_error_t(std::exception_ptr)>>;

template<class State>
struct __inner_recv;

template<class S>
struct __shared_state : std::enable_shared_from_this<__shared_state<S>> {
    using source_t = S;
    using source_cs_t = decltype(std::execution::get_completion_signatures(
        std::declval<S>(), std::declval<empty_env>()));
    using output_cs_t = __output_completion_signatures_t<source_cs_t>;
    using result_t = __result_variant_t<output_cs_t>;
    using inner_recv_t = __inner_recv<__shared_state>;
    using inner_op_t = connect_result_t<S, inner_recv_t>;

    enum class __phase_t { running, done };

    ~__shared_state() noexcept {
        __op_storage.destroy();
    }

    void __start(S sndr) noexcept {
        __keepalive = this->shared_from_this();
        try {
            auto weak = this->weak_from_this();
            auto* op = __op_storage.template emplace_from<inner_op_t>([&]() -> inner_op_t {
                return std::execution::connect(std::move(sndr), inner_recv_t{weak});
            });
            std::execution::start(*op);
        } catch (...) {
            __complete_with(__error_result<std::exception_ptr>{std::current_exception()});
        }
    }

    template<class Alt>
    void __complete_with(Alt alt) noexcept {
        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard lk{__mtx};
            if (__phase == __phase_t::done) {
                return;
            }
            try {
                __result.template emplace<std::decay_t<Alt>>(std::move(alt));
            } catch (...) {
                __result.template emplace<__error_result<std::exception_ptr>>(
                    std::current_exception());
            }
            __phase = __phase_t::done;
            callbacks = std::move(__on_done);
        }

        for (auto& cb : callbacks) {
            cb();
        }

        std::shared_ptr<__shared_state> keepalive;
        {
            std::lock_guard lk{__mtx};
            keepalive = std::move(__keepalive);
        }
    }

    template<class R>
    void __deliver_to(R& rcvr) noexcept {
        std::visit([&](auto& result) noexcept {
            using result_type = std::decay_t<decltype(result)>;
            if constexpr (std::is_same_v<result_type, std::monostate>) {
                std::terminate();
            } else if constexpr (std::is_same_v<result_type, __stopped_result>) {
                if constexpr (requires(R& r) { std::execution::set_stopped(std::move(r)); }) {
                    std::execution::set_stopped(std::move(rcvr));
                } else {
                    std::terminate();
                }
            } else if constexpr (requires { result.__values; }) {
                std::apply([&](auto&... vs) noexcept {
                    std::execution::set_value(std::move(rcvr), vs...);
                }, result.__values);
            } else {
                std::execution::set_error(std::move(rcvr), result.__error);
            }
        }, __result);
    }

    template<class Callback>
    bool __register_or_done(Callback&& cb) {
        std::lock_guard lk{__mtx};
        if (__phase == __phase_t::done) {
            return true;
        }
        __on_done.emplace_back(std::forward<Callback>(cb));
        return false;
    }

    std::mutex __mtx{};
    __phase_t __phase = __phase_t::running;
    result_t __result{};
    std::vector<std::function<void()>> __on_done{};
    __forge_detail::__op_storage<1024> __op_storage{};
    std::shared_ptr<__shared_state> __keepalive{};
};

template<class State>
struct __inner_recv {
    using receiver_concept = receiver_t;

    std::weak_ptr<State> __state;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        auto state = __state.lock();
        if (!state) {
            return;
        }
        using tuple_t = std::tuple<std::decay_t<Vs>...>;
        using alt_t = __value_result<tuple_t>;
        try {
            state->__complete_with(alt_t{tuple_t{static_cast<Vs&&>(vs)...}});
        } catch (...) {
            state->__complete_with(__error_result<std::exception_ptr>{std::current_exception()});
        }
    }

    template<class Error>
    void set_error(Error&& error) && noexcept {
        auto state = __state.lock();
        if (!state) {
            return;
        }
        using alt_t = __error_result<std::decay_t<Error>>;
        try {
            state->__complete_with(alt_t{static_cast<Error&&>(error)});
        } catch (...) {
            state->__complete_with(__error_result<std::exception_ptr>{std::current_exception()});
        }
    }

    void set_stopped() && noexcept {
        auto state = __state.lock();
        if (!state) {
            return;
        }
        state->__complete_with(__stopped_result{});
    }

    auto get_env() const noexcept -> empty_env {
        return {};
    }
};

template<class R>
struct __subscriber {
    explicit __subscriber(R rcvr)
        : __rcvr(std::move(rcvr))
    {}

    std::atomic<bool> __active{true};
    R __rcvr;
};

template<class State, class R>
void __deliver_to_subscriber(const std::shared_ptr<State>& state,
                             const std::shared_ptr<__subscriber<R>>& sub) noexcept {
    if (sub && sub->__active.exchange(false, std::memory_order_acq_rel)) {
        state->__deliver_to(sub->__rcvr);
    }
}

template<class State, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    std::shared_ptr<State> __state;
    std::shared_ptr<__subscriber<R>> __sub;

    __op(std::shared_ptr<State> state, R rcvr)
        : __state(std::move(state))
        , __sub(std::make_shared<__subscriber<R>>(std::move(rcvr)))
    {}

    ~__op() noexcept {
        if (__sub) {
            __sub->__active.store(false, std::memory_order_release);
        }
    }

    void start() & noexcept {
        auto state = __state;
        auto sub = __sub;
        auto weak_sub = std::weak_ptr<__subscriber<R>>{sub};
        bool done = false;
        try {
            done = state->__register_or_done([state, weak_sub]() mutable {
                if (auto locked = weak_sub.lock()) {
                    __deliver_to_subscriber(state, locked);
                }
            });
        } catch (...) {
            std::execution::set_error(std::move(sub->__rcvr), std::current_exception());
            return;
        }
        if (done) {
            __deliver_to_subscriber(state, sub);
        }
    }
};

template<class State>
struct __sender {
    using sender_concept = sender_t;
    using state_t = State;

    std::shared_ptr<State> __state;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> typename std::remove_cvref_t<Self>::state_t::output_cs_t {
        return {};
    }

    template<receiver R>
    auto connect(R rcvr) && -> __op<State, R> {
        return __op<State, R>{std::move(__state), std::move(rcvr)};
    }

    template<receiver R>
    auto connect(R rcvr) const& -> __op<State, R> {
        return __op<State, R>{__state, std::move(rcvr)};
    }

    auto get_env() const noexcept -> empty_env {
        return {};
    }
};

} // namespace __forge_ensure_started

template<sender S>
[[nodiscard]] auto ensure_started(S&& sndr) {
    using source_t = std::decay_t<S>;
    using state_t = __forge_ensure_started::__shared_state<source_t>;

    auto state = std::make_shared<state_t>();
    state->__start(__forge_detail::__copy_or_move_lvalue(std::forward<S>(sndr)));
    return __forge_ensure_started::__sender<state_t>{std::move(state)};
}

} // namespace std::execution
