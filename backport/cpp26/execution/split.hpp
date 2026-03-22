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
#include "env.hpp"

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

    static constexpr std::size_t kOpBuf = 1024;
    alignas(std::max_align_t) unsigned char op_buf[kOpBuf]{};
    void (*op_start)(void*) noexcept = nullptr;
    void (*op_dtor)(void*) noexcept  = nullptr;

    ~__shared_state() noexcept {
        if (op_dtor) { op_dtor(static_cast<void*>(op_buf)); }
    }

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

template<class S>
struct __inner_recv {
    using receiver_concept = receiver_t;
    __shared_state<S>* __st;

    template<class... Vs>
    friend void tag_invoke(set_value_t, __inner_recv&& self, Vs&&... vs) noexcept {
        try {
            {
                std::lock_guard lk{self.__st->mtx};
                self.__st->result.template emplace<1>(
                    std::make_tuple(std::decay_t<Vs>(vs)...));
                self.__st->phase = __shared_state<S>::Phase::done;
            }
            self.__st->notify_all();
        } catch (...) {
            {
                std::lock_guard lk{self.__st->mtx};
                self.__st->result.template emplace<2>(std::current_exception());
                self.__st->phase = __shared_state<S>::Phase::done;
            }
            self.__st->notify_all();
        }
    }
    template<class E>
    friend void tag_invoke(set_error_t, __inner_recv&& self, E&& e) noexcept {
        {
            std::lock_guard lk{self.__st->mtx};
            if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>)
                self.__st->result.template emplace<2>(static_cast<E&&>(e));
            else
                self.__st->result.template emplace<2>(
                    std::make_exception_ptr(static_cast<E&&>(e)));
            self.__st->phase = __shared_state<S>::Phase::done;
        }
        self.__st->notify_all();
    }
    friend void tag_invoke(set_stopped_t, __inner_recv&& self) noexcept {
        {
            std::lock_guard lk{self.__st->mtx};
            self.__st->result.template emplace<3>(__stopped_tag{});
            self.__st->phase = __shared_state<S>::Phase::done;
        }
        self.__st->notify_all();
    }
    friend auto tag_invoke(get_env_t, const __inner_recv&) noexcept -> empty_env {
        return {};
    }
};

template<class S, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    std::shared_ptr<__shared_state<S>> __shared;
    R __rcvr;

    __op(std::shared_ptr<__shared_state<S>> sh, R r)
        : __shared(std::move(sh)), __rcvr(std::move(r)) {}

    friend void tag_invoke(start_t, __op& self) noexcept {
        auto& st = *self.__shared;
        std::unique_lock lk{st.mtx};

        if (st.phase == __shared_state<S>::Phase::done) {
            lk.unlock();
            deliver_result(st, self.__rcvr);
        } else if (st.phase == __shared_state<S>::Phase::idle) {
            st.phase = __shared_state<S>::Phase::running;
            st.on_done.push_back([sh = self.__shared, &rcvr = self.__rcvr]() mutable {
                deliver_result(*sh, rcvr);
            });
            lk.unlock();
            st.op_start(static_cast<void*>(st.op_buf));
        } else {
            st.on_done.push_back([sh = self.__shared, &rcvr = self.__rcvr]() mutable {
                deliver_result(*sh, rcvr);
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
    friend auto tag_invoke(connect_t, __sender self, R r)
        -> __op<S, R>
    {
        return __op<S, R>{std::move(self.__shared), std::move(r)};
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

    static_assert(sizeof(inner_op_t) <= ST::kOpBuf,
        "split: inner op too large for buffer");

    auto shared = std::make_shared<ST>();

    // Connect and store the inner op in the shared buffer
    ::new(static_cast<void*>(shared->op_buf))
        inner_op_t(std::execution::connect(
            std::move(sndr), inner_recv_t{shared.get()}));
    shared->op_start = [](void* p) noexcept {
        std::execution::start(*static_cast<inner_op_t*>(p));
    };
    shared->op_dtor = [](void* p) noexcept {
        static_cast<inner_op_t*>(p)->~inner_op_t();
    };

    return __forge_split::__sender<S>{std::move(shared)};
}

} // namespace std::execution
