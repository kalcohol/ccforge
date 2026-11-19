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

// Protocol model: formal/split; every edit, comments included, requires a source-lock refresh.

#pragma once

#include <execution>

#if defined(FORGE_EXECUTION_BACKPORT_ACTIVE) || \
    (defined(FORGE_NATIVE_SENDERS_COMPLETE) && FORGE_NATIVE_SENDERS_COMPLETE) || \
    (!defined(FORGE_NATIVE_SENDERS_COMPLETE) && defined(__cpp_lib_senders) && __cpp_lib_senders >= 202506L)

#include <forge/detail/completion_meta.hpp>

#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace forge {

namespace __split_detail {

using std::execution::completion_signatures;
using std::execution::set_error_t;
using std::execution::set_stopped_t;
using std::execution::set_value_t;

namespace meta = forge::__detail::meta;

template<class S>
using source_signatures_t =
    std::execution::completion_signatures_of_t<S, std::execution::env<>>;

template<class CS>
struct sends_stopped_from;

template<class... Sigs>
struct sends_stopped_from<completion_signatures<Sigs...>>
    : std::bool_constant<(std::is_same_v<Sigs, set_stopped_t()> || ...)> {};

// Each mapped input below contributes either zero signatures or one.
template<class Accumulated, class... Inputs>
struct concat_unique_signatures;

template<class Accumulated>
struct concat_unique_signatures<Accumulated> : std::type_identity<Accumulated> {};

template<class Accumulated, class... Rest>
struct concat_unique_signatures<Accumulated, completion_signatures<>, Rest...>
    : concat_unique_signatures<Accumulated, Rest...> {};

template<class... Out, class Sig, class... Rest>
struct concat_unique_signatures<completion_signatures<Out...>, completion_signatures<Sig>, Rest...>
    : concat_unique_signatures<std::conditional_t<(std::is_same_v<Sig, Out> || ...),
          completion_signatures<Out...>, completion_signatures<Out..., Sig>>, Rest...> {};

struct __immovable {
    __immovable() = default;
    __immovable(__immovable&&) = delete;
};

template<class T>
constexpr decltype(auto) __forward_as_given(T&& value) noexcept {
    return static_cast<T&&>(value);
}

struct __stopped_tag {};

template<class Sig>
struct __cached_completion {
    using type = completion_signatures<>;
    static constexpr std::size_t value_count = 0;
};

template<class... Vs>
struct __cached_completion<set_value_t(Vs...)> {
    using type = completion_signatures<set_value_t(const std::decay_t<Vs>&...)>;
    static constexpr std::size_t value_count = 1;
};

template<>
struct __cached_completion<set_stopped_t()> {
    using type = completion_signatures<set_stopped_t()>;
    static constexpr std::size_t value_count = 0;
};

template<class CS>
struct __cached_completion_signatures;

template<class... Sigs>
struct __cached_completion_signatures<completion_signatures<Sigs...>> {
    static_assert(
        (std::size_t{0} + ... + __cached_completion<Sigs>::value_count) <= 1,
        "split supports at most one value completion shape");
    using type = typename concat_unique_signatures<
        completion_signatures<>,
        typename __cached_completion<Sigs>::type...,
        completion_signatures<set_error_t(std::exception_ptr)>>::type;
};

template<class CS>
using __cached_completion_signatures_t =
    typename __cached_completion_signatures<CS>::type;

// A named concept rather than a requires-expression default argument: MSVC
// rejects the failed call inside the latter instead of yielding false.
template<class S>
concept __source_has_allocator = requires(const S& s) {
    std::get_allocator(std::execution::get_env(s));
};

template<class S, bool = __source_has_allocator<S>>
struct __allocator_for_source {
    using type = std::allocator<void>;
    static type get(const S&) noexcept { return {}; }
};

template<class S>
struct __allocator_for_source<S, true> {
    using type = std::decay_t<decltype(std::get_allocator(
        std::execution::get_env(std::declval<const S&>())))>;
    static type get(const S& s) {
        return std::get_allocator(std::execution::get_env(s));
    }
};

template<class S>
struct __shared_state;

template<class S>
struct __inner_recv {
    using receiver_concept = std::execution::receiver_tag;
    __shared_state<S>* __st;

    template<class... Vs> void set_value(Vs&&... vs) && noexcept;
    template<class E> void set_error(E&& e) && noexcept;
    void set_stopped() && noexcept;
    auto get_env() const noexcept -> std::execution::env<> { return {}; }
};

template<class S>
struct __subscriber_base {
    std::shared_ptr<__subscriber_base> next;
    virtual ~__subscriber_base() = default;
    virtual void deliver(__shared_state<S>&) noexcept = 0;
};

template<class S>
struct __shared_state : std::enable_shared_from_this<__shared_state<S>> {
    using cs_t = source_signatures_t<S>;
    using value_tuple_t = meta::single_value_tuple_t<cs_t>;

    enum class Phase { idle, running, done };

    std::mutex mtx{};
    Phase phase = Phase::idle;
    std::variant<std::monostate, value_tuple_t, std::exception_ptr, __stopped_tag> result{};
    std::shared_ptr<__subscriber_base<S>> on_done;
    __subscriber_base<S>* tail = nullptr;
    std::shared_ptr<__shared_state> keepalive;
    using allocator_t = typename __allocator_for_source<std::decay_t<S>>::type;
    allocator_t allocator;
    using op_t = std::execution::connect_result_t<S, __inner_recv<S>>;
    op_t op;

    template<class Source>
    explicit __shared_state(Source&& source, allocator_t alloc)
        : allocator(std::move(alloc))
        , op(std::execution::connect(static_cast<Source&&>(source), __inner_recv<S>{this}))
    {}

    void notify_all() noexcept {
        std::shared_ptr<__subscriber_base<S>> callbacks;
        std::shared_ptr<__shared_state> pin;
        {
            std::lock_guard lk{mtx};
            callbacks = std::move(on_done);
            tail = nullptr;
            pin = std::move(keepalive);
        }
        while (callbacks) {
            auto next = std::move(callbacks->next);
            callbacks->deliver(*this);
            callbacks = std::move(next);
        }
    }
};

template<class S, class OuterRecv>
void deliver_result(__shared_state<S>& st, OuterRecv& rcvr) noexcept {
    std::visit([&](const auto& v) {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, std::monostate>) {
            // Invariant violation: subscribers are only delivered after the
            // source operation stored value/error/stopped and marked done.
            std::terminate();
        } else if constexpr (std::is_same_v<V, typename __shared_state<S>::value_tuple_t>) {
            if constexpr (!meta::type_list_empty_v<
                              meta::value_tuple_list_t<
                                  typename __shared_state<S>::cs_t>>) {
                std::apply([&](const auto&... vs) {
                    std::execution::set_value(std::move(rcvr), vs...);
                }, v);
            } else {
                std::terminate();
            }
        } else if constexpr (std::is_same_v<V, std::exception_ptr>) {
            std::execution::set_error(std::move(rcvr), std::exception_ptr{v});
        } else {
            if constexpr (sends_stopped_from<typename __shared_state<S>::cs_t>::value) {
                std::execution::set_stopped(std::move(rcvr));
            } else {
                std::terminate();
            }
        }
    }, st.result);
}

template<class S, class R>
struct __subscriber : __subscriber_base<S> {
    explicit __subscriber(R r) : rcvr(std::move(r)) {}

    void deliver(__shared_state<S>& state) noexcept override {
        if (active.exchange(false, std::memory_order_acq_rel)) {
            deliver_result(state, rcvr);
        }
    }

    std::atomic<bool> active{true};
    R rcvr;
};

template<class S, class R>
void deliver_to_subscriber(const std::shared_ptr<__shared_state<S>>& sh,
                           const std::shared_ptr<__subscriber<S, R>>& sub) noexcept {
    if (sub && sub->active.exchange(false, std::memory_order_acq_rel)) {
        deliver_result(*sh, sub->rcvr);
    }
}

template<class S>
template<class... Vs>
void __inner_recv<S>::set_value(Vs&&... vs) && noexcept {
    auto st = __st->shared_from_this();
    {
        std::lock_guard lk{st->mtx};
        try {
            st->result.template emplace<1>(static_cast<Vs&&>(vs)...);
        } catch (...) {
            st->result.template emplace<2>(std::current_exception());
        }
        st->phase = __shared_state<S>::Phase::done;
    }
    st->notify_all();
}

template<class S>
template<class E>
void __inner_recv<S>::set_error(E&& e) && noexcept {
    auto st = __st->shared_from_this();
    {
        std::lock_guard lk{st->mtx};
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
            st->result.template emplace<2>(static_cast<E&&>(e));
        } else {
            try { throw static_cast<E&&>(e); }
            catch (...) { st->result.template emplace<2>(std::current_exception()); }
        }
        st->phase = __shared_state<S>::Phase::done;
    }
    st->notify_all();
}

template<class S>
void __inner_recv<S>::set_stopped() && noexcept {
    auto st = __st->shared_from_this();
    {
        std::lock_guard lk{st->mtx};
        st->result.template emplace<3>(__stopped_tag{});
        st->phase = __shared_state<S>::Phase::done;
    }
    st->notify_all();
}

template<class S, class R>
struct __op : __immovable {
    using operation_state_concept = std::execution::operation_state_tag;

    std::shared_ptr<__shared_state<S>> __shared;
    std::shared_ptr<__subscriber<S, R>> __sub;

    __op(std::shared_ptr<__shared_state<S>> sh, R r)
        : __shared(std::move(sh))
        , __sub(std::allocate_shared<__subscriber<S, R>>(
              __shared->allocator, std::move(r)))
    {}

    ~__op() {
        if (__sub) {
            __sub->active.store(false, std::memory_order_release);
        }
    }

    void start() & noexcept {
        auto sh = __shared;
        auto sub = __sub;
        auto& st = *sh;
        std::unique_lock lk{st.mtx};
        if (st.phase == __shared_state<S>::Phase::done) {
            lk.unlock();
            deliver_to_subscriber(sh, sub);
            return;
        }
        if (st.tail) st.tail->next = sub;
        else st.on_done = sub;
        st.tail = sub.get();
        if (st.phase == __shared_state<S>::Phase::idle) {
            st.phase = __shared_state<S>::Phase::running;
            st.keepalive = sh;
            lk.unlock();
            std::execution::start(st.op);
        }
    }
};

template<class S>
struct __sender {
    using sender_concept = std::execution::sender_tag;
    using source_t = S;

    std::shared_ptr<__shared_state<S>> __shared;

    template<class Self, class... Envs>
    static constexpr auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using source_cs_t = source_signatures_t<typename self_t::source_t>;
        return __cached_completion_signatures_t<source_cs_t>{};
    }

    template<std::execution::receiver R>
    auto connect(R r) && -> __op<S, R>
    {
        return __op<S, R>{std::move(__shared), std::move(r)};
    }

    template<std::execution::receiver R>
    auto connect(R r) const& -> __op<S, R>
    {
        return __op<S, R>{__shared, std::move(r)};
    }

    auto get_env() const noexcept -> std::execution::env<> {
        return {};
    }
};

} // namespace __split_detail

template<class S>
    requires std::execution::sender_in<S, std::execution::env<>> &&
             requires {
                 std::execution::connect(std::declval<S>(),
                     std::declval<__split_detail::__inner_recv<S>>());
             }
[[nodiscard]] auto split(S&& sndr) {
    using source_t = std::decay_t<S>;
    using ST = __split_detail::__shared_state<S>;
    auto alloc = __split_detail::__allocator_for_source<source_t>::get(sndr);
    auto shared = std::allocate_shared<ST>(alloc,
        __split_detail::__forward_as_given(std::forward<S>(sndr)), alloc);

    return __split_detail::__sender<S>{std::move(shared)};
}

} // namespace forge

#endif
