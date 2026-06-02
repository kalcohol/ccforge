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
#include <variant>
#include <vector>

namespace std::execution {

namespace __forge_split {

struct __stopped_tag {};

template<class S>
struct __shared_state {
    using cs_t = decltype(std::execution::get_completion_signatures(
        std::declval<S>(), std::execution::empty_env{}));
    using value_tuple_t = std::execution::__forge_meta::__single_value_tuple_t<cs_t>;

    enum class Phase { idle, running, done };

    std::mutex mtx{};
    Phase phase = Phase::idle;
    std::variant<std::monostate, value_tuple_t, std::exception_ptr, __stopped_tag> result{};
    std::vector<std::function<void()>> on_done{};

    __forge_detail::__op_storage<1024> op_storage{};
    void* op_ptr = nullptr;
    void (*op_start)(void*) noexcept = nullptr;

    void notify_all() noexcept {
        std::vector<std::function<void()>> cbs;
        {
            std::lock_guard lk{mtx};
            cbs = std::move(on_done);
        }
        for (auto& cb : cbs) cb();
    }
};

template<class S, class OuterRecv>
void deliver_result(__shared_state<S>& st, OuterRecv& rcvr) noexcept {
    std::visit([&](auto& v) {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, std::monostate>) {
            // Invariant violation: subscribers are only delivered after the
            // source operation stored value/error/stopped and marked done.
            std::terminate();
        } else if constexpr (std::is_same_v<V, typename __shared_state<S>::value_tuple_t>) {
            std::apply([&](auto&... vs) {
                std::execution::set_value(std::move(rcvr), vs...);
            }, v);
        } else if constexpr (std::is_same_v<V, std::exception_ptr>) {
            std::execution::set_error(std::move(rcvr), v);
        } else {
            std::execution::set_stopped(std::move(rcvr));
        }
    }, st.result);
}

template<class R>
struct __subscriber {
    explicit __subscriber(R r) : rcvr(std::move(r)) {}

    std::atomic<bool> active{true};
    R rcvr;
};

template<class S, class R>
void deliver_to_subscriber(const std::shared_ptr<__shared_state<S>>& sh,
                           const std::shared_ptr<__subscriber<R>>& sub) noexcept {
    if (sub && sub->active.exchange(false, std::memory_order_acq_rel)) {
        deliver_result(*sh, sub->rcvr);
    }
}

template<class S>
struct __inner_recv {
    using receiver_concept = receiver_t;
    std::weak_ptr<__shared_state<S>> __st;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        auto st = __st.lock();
        if (!st) { return; }
        try {
            {
                std::lock_guard lk{st->mtx};
                st->result.template emplace<1>(
                    std::make_tuple(std::decay_t<Vs>(vs)...));
                st->phase = __shared_state<S>::Phase::done;
            }
            st->notify_all();
        } catch (...) {
            {
                std::lock_guard lk{st->mtx};
                st->result.template emplace<2>(std::current_exception());
                st->phase = __shared_state<S>::Phase::done;
            }
            st->notify_all();
        }
    }
    template<class E>
    void set_error(E&& e) && noexcept {
        auto st = __st.lock();
        if (!st) { return; }
        {
            std::lock_guard lk{st->mtx};
            if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>)
                st->result.template emplace<2>(static_cast<E&&>(e));
            else
                st->result.template emplace<2>(
                    std::make_exception_ptr(static_cast<E&&>(e)));
            st->phase = __shared_state<S>::Phase::done;
        }
        st->notify_all();
    }
    void set_stopped() && noexcept {
        auto st = __st.lock();
        if (!st) { return; }
        {
            std::lock_guard lk{st->mtx};
            st->result.template emplace<3>(__stopped_tag{});
            st->phase = __shared_state<S>::Phase::done;
        }
        st->notify_all();
    }
    auto get_env() const noexcept -> empty_env {
        return {};
    }
};

template<class S, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    std::shared_ptr<__shared_state<S>> __shared;
    std::shared_ptr<__subscriber<R>> __sub;

    __op(std::shared_ptr<__shared_state<S>> sh, R r)
        : __shared(std::move(sh)), __sub(std::make_shared<__subscriber<R>>(std::move(r))) {}

    ~__op() {
        if (__sub) {
            __sub->active.store(false, std::memory_order_release);
        }
    }

    void start() & noexcept {
        auto sh = __shared;
        auto sub = __sub;
        auto weak_sub = std::weak_ptr<__subscriber<R>>{sub};
        auto& st = *sh;
        std::unique_lock lk{st.mtx};

        if (st.phase == __shared_state<S>::Phase::done) {
            lk.unlock();
            deliver_to_subscriber(sh, sub);
        } else if (st.phase == __shared_state<S>::Phase::idle) {
            st.phase = __shared_state<S>::Phase::running;
            st.on_done.push_back([sh, weak_sub]() mutable {
                if (auto locked = weak_sub.lock()) {
                    deliver_to_subscriber(sh, locked);
                }
            });
            lk.unlock();
            st.op_start(st.op_ptr);
        } else {
            st.on_done.push_back([sh, weak_sub]() mutable {
                if (auto locked = weak_sub.lock()) {
                    deliver_to_subscriber(sh, locked);
                }
            });
        }
    }
};

template<class S>
struct __sender {
    using sender_concept = sender_t;
    using source_t = S;

    std::shared_ptr<__shared_state<S>> __shared;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        return decltype(std::execution::get_completion_signatures(
            std::declval<typename self_t::source_t>(),
            std::declval<Env>())){};
    }

    template<receiver R>
    auto connect(R r) && -> __op<S, R>
    {
        return __op<S, R>{std::move(__shared), std::move(r)};
    }

    template<receiver R>
    auto connect(R r) const& -> __op<S, R>
    {
        return __op<S, R>{__shared, std::move(r)};
    }

    auto get_env() const noexcept -> empty_env {
        return {};
    }
};

} // namespace __forge_split

template<sender S>
[[nodiscard]] auto split(S&& sndr) {
    using source_t = std::decay_t<S>;
    using ST = __forge_split::__shared_state<source_t>;
    using inner_recv_t = __forge_split::__inner_recv<source_t>;
    using inner_op_t   = connect_result_t<source_t, inner_recv_t>;

    auto shared = std::make_shared<ST>();

    shared->op_ptr = shared->op_storage.template emplace_from<inner_op_t>([&]() -> inner_op_t {
        return std::execution::connect(
            __forge_detail::__forward_as_given(std::forward<S>(sndr)),
            inner_recv_t{std::weak_ptr<ST>{shared}});
    });
    shared->op_start = [](void* p) noexcept {
        std::execution::start(*static_cast<inner_op_t*>(p));
    };

    return __forge_split::__sender<source_t>{std::move(shared)};
}

} // namespace std::execution
