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

#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

#include "concepts.hpp"
#include "detail/proxy_stop_adapter.hpp"
#include "env.hpp"

namespace std::execution::parallel_scheduler_replacement {

struct receiver_proxy {
protected:
    virtual bool __query_env(inplace_stop_token&) const noexcept = 0; // exposition only

public:
    virtual void set_value() noexcept = 0;
    virtual void set_error(exception_ptr) noexcept = 0;
    virtual void set_stopped() noexcept = 0;

    template<class P, class Query>
        requires std::is_class_v<Query> && std::same_as<Query, std::decay_t<Query>>
    optional<P> try_query(Query) const noexcept {
        static_assert(std::is_object_v<P> && !std::is_array_v<P> &&
                      std::same_as<P, std::remove_cv_t<P>>,
                      "try_query requires a cv-unqualified non-array object type");
        if constexpr (std::same_as<Query, get_stop_token_t> && std::same_as<P, inplace_stop_token>) {
            inplace_stop_token __token;
            if (__query_env(__token)) return __token;
        }
        return nullopt;
    }
};

struct bulk_item_receiver_proxy : receiver_proxy {
    virtual void execute(size_t, size_t) noexcept = 0;
};

struct parallel_scheduler_backend {
    virtual ~parallel_scheduler_backend() = default;

    virtual void schedule(receiver_proxy&, span<byte>) noexcept = 0;
    virtual void schedule_bulk_chunked(size_t, bulk_item_receiver_proxy&, span<byte>) noexcept = 0;
    virtual void schedule_bulk_unchunked(size_t, bulk_item_receiver_proxy&, span<byte>) noexcept = 0;
};

} // namespace std::execution::parallel_scheduler_replacement

namespace std::execution::__forge_parsched_detail {

template<class Read>
concept __proxy_token_read = requires(const Read& __read) { __read(); };

template<class Token, class Observer>
consteval bool __proxy_needs_adapter() {
    if constexpr (std::same_as<Token, std::inplace_stop_token> || __proxy_unstoppable_token<Token>) {
        return false;
    } else {
        constexpr bool __compatible = __proxy_stoppable_callback<Token, __proxy_stop_forwarder<Observer>>;
        static_assert(__compatible,
                      "stoppable_token callback_type must accept the proxy stop forwarder "
                      "with an upstream token rvalue, mutable lvalue, and const lvalue");
        return __compatible;
    }
}

template<class Read, class Observer, bool = __proxy_token_read<Read>>
class __proxy_stop_query {
public:
    explicit __proxy_stop_query(Observer) noexcept {}
    bool __query_env(const Read&, std::inplace_stop_token&) const noexcept { return false; }
    void __finish() noexcept {}
};

template<class Read, class Observer>
class __proxy_stop_query<Read, Observer, true> {
    using __token = std::remove_cvref_t<decltype(std::declval<const Read&>()())>;
    static constexpr bool __adapt = __proxy_needs_adapter<__token, Observer>();

    struct __direct {
        explicit __direct(Observer) noexcept {}
    };
    [[no_unique_address]] std::conditional_t<__adapt, __proxy_stop_adapter<__token, Observer>, __direct>
        __adapter_;

public:
    explicit __proxy_stop_query(Observer __observe) : __adapter_(std::move(__observe)) {}

    bool __query_env(const Read& __read, std::inplace_stop_token& __result) const noexcept {
        if constexpr (__adapt) {
            __result = __adapter_.__query(__read);
        } else if constexpr (std::same_as<__token, std::inplace_stop_token>) {
            __result = __read();
        } else {
            __result = {};
        }
        return true;
    }

    void __finish() noexcept {
        if constexpr (__adapt) __adapter_.__finish();
    }
};

template<class Receiver, class Query = std::get_stop_token_t>
struct __read_proxy_env {
    const Receiver& __receiver;

    auto operator()() const noexcept
        requires requires { Query{}(std::execution::get_env(__receiver)); }
    {
        // The query may return a reference into a temporary environment.
        // Construct the returned token before that full-expression ends.
        return Query{}(std::execution::get_env(__receiver));
    }
};

template<class Receiver, class Base = parallel_scheduler_replacement::receiver_proxy,
         class Observer = __no_proxy_stop_observer>
class __receiver_proxy : public Base {
    Receiver __receiver_;
    [[no_unique_address]] __proxy_stop_query<__read_proxy_env<Receiver>, Observer> __stop_;

protected:
    bool __query_env(std::inplace_stop_token& __token) const noexcept final {
        return __stop_.__query_env({__receiver_}, __token);
    }

public:
    explicit __receiver_proxy(Receiver __receiver, Observer __observe = {})
        : __receiver_(std::move(__receiver)), __stop_(std::move(__observe)) {}

    __receiver_proxy(const __receiver_proxy&) = delete;
    __receiver_proxy& operator=(const __receiver_proxy&) = delete;

    void set_value() noexcept final {
        __stop_.__finish();
        std::execution::set_value(std::move(__receiver_));
    }

    void set_error(std::exception_ptr __error) noexcept final {
        __stop_.__finish();
        std::execution::set_error(std::move(__receiver_), std::move(__error));
    }

    void set_stopped() noexcept final {
        __stop_.__finish();
        std::execution::set_stopped(std::move(__receiver_));
    }
};

template<class Receiver, class Item, class Observer = __no_proxy_stop_observer>
class __bulk_item_receiver_proxy final
    : public __receiver_proxy<Receiver, parallel_scheduler_replacement::bulk_item_receiver_proxy, Observer> {
    using __base = __receiver_proxy<Receiver, parallel_scheduler_replacement::bulk_item_receiver_proxy,
                                    Observer>;
    [[no_unique_address]] Item __item_;

public:
    __bulk_item_receiver_proxy(Receiver __receiver, Item __item, Observer __observe = {})
        : __base(std::move(__receiver), std::move(__observe)), __item_(std::move(__item)) {}

    void execute(std::size_t __begin, std::size_t __end) noexcept override {
        static_assert(noexcept(__item_(__begin, __end)));
        __item_(__begin, __end);
    }
};

// Shared operation-side ownership for the ordinary and bulk SPI. Backends may
// ignore the span or use their own fallback. All backend accesses to the proxy
// and scratch end by terminal entry, which may free this entire operation.
template<class Proxy, std::size_t Bytes = 64>
class __backend_operation {
    std::shared_ptr<parallel_scheduler_replacement::parallel_scheduler_backend> __backend_;
    alignas(std::max_align_t) std::array<std::byte, Bytes> __storage_{};
    Proxy __proxy_;

public:
    template<class... Args>
    explicit __backend_operation(
        std::shared_ptr<parallel_scheduler_replacement::parallel_scheduler_backend> __backend,
        Args&&... __args)
        : __backend_(std::move(__backend)), __proxy_(std::forward<Args>(__args)...) {}

    __backend_operation(const __backend_operation&) = delete;
    __backend_operation& operator=(const __backend_operation&) = delete;

    void __start() noexcept { __backend_->schedule(__proxy_, __storage_); }

    void __start_chunked(std::size_t __shape) noexcept {
        __backend_->schedule_bulk_chunked(__shape, __proxy_, __storage_);
    }

    void __start_unchunked(std::size_t __shape) noexcept {
        __backend_->schedule_bulk_unchunked(__shape, __proxy_, __storage_);
    }
};

} // namespace std::execution::__forge_parsched_detail
