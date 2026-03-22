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
#include "stop_token.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <tuple>
#include <variant>

namespace std::execution {

namespace __forge_when_all {

struct __stopped_tag {};

template<class S>
using __sender_value_tuple_t = std::execution::__forge_meta::__single_value_tuple_t<
    decltype(std::execution::get_completion_signatures(
        std::declval<S>(), std::execution::empty_env{}))>;

template<class OuterRecv, class... Senders>
struct __op;

template<std::size_t I, class OuterRecv, class... Senders>
struct __child_recv {
    using receiver_concept = receiver_t;
    __op<OuterRecv, Senders...>* __self;

    template<class... Vs>
    friend void tag_invoke(set_value_t, __child_recv&& self, Vs&&... vs) noexcept {
        try {
            std::get<I>(self.__self->__partial) =
                std::make_tuple(std::decay_t<Vs>(vs)...);
        } catch (...) {
            self.__self->child_fail(std::current_exception());
            return;
        }
        self.__self->child_done();
    }
    template<class E>
    friend void tag_invoke(set_error_t, __child_recv&& self, E&& e) noexcept {
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>)
            self.__self->child_fail(static_cast<E&&>(e));
        else
            self.__self->child_fail(std::make_exception_ptr(static_cast<E&&>(e)));
    }
    friend void tag_invoke(set_stopped_t, __child_recv&& self) noexcept {
        self.__self->child_stop();
    }
    friend auto tag_invoke(get_env_t, const __child_recv& self) noexcept {
        return std::execution::make_env(
            std::execution::make_prop(
                get_stop_token_t{},
                self.__self->__stop_src.get_token()));
    }
};

template<class OuterRecv, class... Senders>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    static constexpr std::size_t N = sizeof...(Senders);

    // Combined value tuple type for all senders (concatenated)
    using all_values_t = std::execution::__forge_meta::__tuple_cat_t<
        __sender_value_tuple_t<Senders>...>;

    OuterRecv __outer_recv;
    inplace_stop_source __stop_src;
    std::atomic<std::size_t> __pending{N};
    std::atomic<bool> __failed{false};
    std::mutex __mtx;
    std::variant<std::monostate, all_values_t, std::exception_ptr, __stopped_tag> __result;
    std::tuple<std::optional<__sender_value_tuple_t<Senders>>...> __partial;

    static constexpr std::size_t kChildBuf = 512;
    alignas(std::max_align_t) unsigned char __child_bufs[N][kChildBuf];
    void (*__child_starts[N])(void*) noexcept;
    void (*__child_dtors[N])(void*) noexcept;

    template<std::size_t... Is>
    __op(OuterRecv r, std::tuple<Senders...> sndrs, std::index_sequence<Is...>)
        : __outer_recv(std::move(r))
    {
        (..., init_child<Is>(std::get<Is>(std::move(sndrs))));
    }

    template<std::size_t I>
    void init_child(std::tuple_element_t<I, std::tuple<Senders...>> sndr) {
        using S = std::tuple_element_t<I, std::tuple<Senders...>>;
        using child_op_t = connect_result_t<S, __child_recv<I, OuterRecv, Senders...>>;
        static_assert(sizeof(child_op_t) <= kChildBuf,
            "when_all: child op too large for buffer");
        ::new(static_cast<void*>(__child_bufs[I]))
            child_op_t(std::execution::connect(
                std::move(sndr),
                __child_recv<I, OuterRecv, Senders...>{this}));
        __child_starts[I] = [](void* p) noexcept {
            std::execution::start(*static_cast<child_op_t*>(p));
        };
        __child_dtors[I] = [](void* p) noexcept {
            static_cast<child_op_t*>(p)->~child_op_t();
        };
    }

    ~__op() {
        for (std::size_t i = 0; i < N; ++i)
            if (__child_dtors[i]) __child_dtors[i](static_cast<void*>(__child_bufs[i]));
    }

    void child_fail(std::exception_ptr ep) noexcept {
        bool expected = false;
        if (__failed.compare_exchange_strong(expected, true)) {
            std::lock_guard lk{__mtx};
            __result.template emplace<2>(std::move(ep));
        }
        __stop_src.request_stop();
        child_done();
    }

    void child_stop() noexcept {
        bool expected = false;
        if (__failed.compare_exchange_strong(expected, true)) {
            std::lock_guard lk{__mtx};
            __result.template emplace<3>(__stopped_tag{});
        }
        __stop_src.request_stop();
        child_done();
    }

    void child_done() noexcept {
        if (--__pending == 0) deliver();
    }

    void deliver() noexcept {
        if (__result.index() == 2) {
            set_error(std::move(__outer_recv), std::get<2>(__result));
        } else if (__result.index() == 3) {
            set_stopped(std::move(__outer_recv));
        } else {
            try_deliver_values(std::index_sequence_for<Senders...>{});
        }
    }

    template<std::size_t... Is>
    void try_deliver_values(std::index_sequence<Is...>) noexcept {
        try {
            auto combined = std::tuple_cat(*std::get<Is>(__partial)...);
            std::apply([&](auto&&... vs) {
                set_value(std::move(__outer_recv), std::move(vs)...);
            }, std::move(combined));
        } catch (...) {
            set_error(std::move(__outer_recv), std::current_exception());
        }
    }

    friend void tag_invoke(start_t, __op& self) noexcept {
        for (std::size_t i = 0; i < self.N; ++i)
            self.__child_starts[i](static_cast<void*>(self.__child_bufs[i]));
    }
};

template<class... Senders>
struct __sender {
    using sender_concept = sender_t;
    std::tuple<Senders...> __sndrs;

    friend auto tag_invoke(get_completion_signatures_t,
                           const __sender&, auto env) noexcept {
        using cart_t = std::execution::__forge_meta::__cartesian_product_value_sigs_t<
            decltype(std::execution::get_completion_signatures(
                std::declval<Senders>(), env))...>;
        using err_cs = completion_signatures<set_error_t(std::exception_ptr)>;
        using stp_cs = completion_signatures<set_stopped_t()>;
        return std::execution::__forge_meta::__concat_cs_t<cart_t, err_cs, stp_cs>{};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, __sender self, R r) {
        return __op<R, Senders...>{
            std::move(r),
            std::move(self.__sndrs),
            std::index_sequence_for<Senders...>{}};
    }

    friend auto tag_invoke(get_env_t, const __sender&) noexcept -> empty_env {
        return {};
    }
};

} // namespace __forge_when_all

template<sender... Senders>
[[nodiscard]] auto when_all(Senders... sndrs) {
    return __forge_when_all::__sender<Senders...>{
        std::tuple<Senders...>{std::move(sndrs)...}};
}

} // namespace std::execution
