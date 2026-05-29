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
            // should not happen
        } else if constexpr (std::is_same_v<V, typename __shared_state<S>::value_tuple_t>) {
            std::apply([&](auto&... vs) {
                set_value(std::move(rcvr), vs...);
            }, v);
        } else if constexpr (std::is_same_v<V, std::exception_ptr>) {
            set_error(std::move(rcvr), v);
        } else {
            set_stopped(std::move(rcvr));
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
    friend void tag_invoke(set_value_t, __inner_recv&& self, Vs&&... vs) noexcept {
        auto st = self.__st.lock();
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
    friend void tag_invoke(set_error_t, __inner_recv&& self, E&& e) noexcept {
        auto st = self.__st.lock();
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
    friend void tag_invoke(set_stopped_t, __inner_recv&& self) noexcept {
        auto st = self.__st.lock();
        if (!st) { return; }
        {
            std::lock_guard lk{st->mtx};
            st->result.template emplace<3>(__stopped_tag{});
            st->phase = __shared_state<S>::Phase::done;
        }
        st->notify_all();
    }
    friend auto tag_invoke(get_env_t, const __inner_recv&) noexcept -> empty_env {
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

    friend void tag_invoke(start_t, __op& self) noexcept {
        auto sh = self.__shared;
        auto sub = self.__sub;
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
    std::shared_ptr<__shared_state<S>> __shared;

    friend auto tag_invoke(get_completion_signatures_t,
                           const __sender& self, auto env) noexcept {
        return decltype(std::execution::get_completion_signatures(
            std::declval<S>(), env)){};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, __sender&& self, R r)
        -> __op<S, R>
    {
        return __op<S, R>{std::move(self.__shared), std::move(r)};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, const __sender& self, R r)
        -> __op<S, R>
    {
        return __op<S, R>{self.__shared, std::move(r)};
    }

    friend auto tag_invoke(get_env_t, const __sender&) noexcept -> empty_env {
        return {};
    }
};

} // namespace __forge_split

template<sender S>
[[nodiscard]] auto split(S sndr) {
    using ST = __forge_split::__shared_state<S>;
    using inner_recv_t = __forge_split::__inner_recv<S>;
    using inner_op_t   = connect_result_t<S, inner_recv_t>;

    auto shared = std::make_shared<ST>();

    shared->op_ptr = shared->op_storage.template emplace_from<inner_op_t>([&]() -> inner_op_t {
        return std::execution::connect(
            std::move(sndr), inner_recv_t{std::weak_ptr<ST>{shared}});
    });
    shared->op_start = [](void* p) noexcept {
        std::execution::start(*static_cast<inner_op_t*>(p));
    };

    return __forge_split::__sender<S>{std::move(shared)};
}

} // namespace std::execution
