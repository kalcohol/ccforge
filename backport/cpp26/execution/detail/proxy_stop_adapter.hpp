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

#include <concepts>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "../stop_token.hpp"
#include "sync_shim.hpp"

namespace std::execution::__forge_parsched_detail {

// [stoptoken.concepts]/4 tests `tok.stop_possible()` on a const requires-
// parameter. That needs P2280, which Clang 19 rejects in this form. Use only
// the portable static form, as the backport's unstoppable_token does in
// stop_token.hpp. A token with only a non-static observer takes the adapter
// path and is ill-formed if its callback fails the proxy's static_assert.
template<class Token>
concept __proxy_static_stop_impossible = requires {
    requires std::bool_constant<(!Token::stop_possible())>::value;
};

template<class Token>
concept __proxy_unstoppable_token = std::stoppable_token<Token> &&
    __proxy_static_stop_impossible<Token>;

template<class Token, class Forwarder>
concept __proxy_stoppable_callback = std::__forge_stop_detail::__stoppable_token_for<Token, Forwarder> &&
    std::constructible_from<std::stop_callback_for_t<Token, Forwarder>, const Token&, Forwarder>;

enum class __proxy_stop_event {
    first_query, waiting, register_callback, publish, read_token,
    forward_request, terminal_commit, unregister_callback
};

struct __no_proxy_stop_observer {
    void operator()(__proxy_stop_event) const noexcept {}
};

using __proxy_stop_cell = std::optional<std::inplace_stop_source>;

template<class Observer>
struct __proxy_stop_forwarder {
    std::shared_ptr<__proxy_stop_cell> __source;
    [[no_unique_address]] Observer __observe;

    void operator()() const noexcept {
        // A downstream callback can complete and delete the operation on
        // this very thread, including this upstream callback object.
        auto __pin = __source;
        __observe(__proxy_stop_event::forward_request);
        (*__pin)->request_stop();
    }
};

// Protocol model: formal/proxy_stop_adapter. The source and the registration
// are lazy. Reserve the source cell at operation construction, where allocation
// can throw, rather than inside the backend's noexcept query.
template<class Token, class Observer = __no_proxy_stop_observer>
class __proxy_stop_adapter {
    using __forward_stop = __proxy_stop_forwarder<Observer>;

    enum : unsigned { __fresh, __initializing, __ready };

    [[no_unique_address]] Observer __observe_;
    std::shared_ptr<__proxy_stop_cell> __source_ = std::make_shared<__proxy_stop_cell>();
    mutable std::optional<std::stop_callback_for_t<Token, __forward_stop>> __callback_;
    mutable __forge_sync::atomic<unsigned> __phase_{__fresh};

public:
    explicit __proxy_stop_adapter(Observer __observe = {})
        : __observe_(std::move(__observe)) {}

    template<class Read>
    std::inplace_stop_token __query(Read&& __read) const noexcept {
        unsigned __expected = __fresh;
        if (__phase_.compare_exchange_strong(__expected, __initializing)) {
            __observe_(__proxy_stop_event::first_query);
            auto __upstream = std::forward<Read>(__read)();
            if (__upstream.stop_possible()) {
                __source_->emplace();
                __observe_(__proxy_stop_event::register_callback);
                // Registration itself handles an earlier or concurrent request.
                __callback_.emplace(std::move(__upstream), __forward_stop{__source_, __observe_});
            }
            __observe_(__proxy_stop_event::publish);
            __phase_.store(__ready);
            __phase_.notify_all();
        } else if (__expected == __initializing) {
            __observe_(__proxy_stop_event::waiting);
            __phase_.wait(__initializing);
        }
        __observe_(__proxy_stop_event::read_token);
        return *__source_ ? (*__source_)->get_token() : std::inplace_stop_token{};
    }

    // The backend ends all proxy queries and execute calls before terminal
    // dispatch. It need not serialize a pending upstream stop with this call.
    void __finish() noexcept {
        __observe_(__proxy_stop_event::terminal_commit);
        if (__callback_) {
            __callback_.reset();
            __observe_(__proxy_stop_event::unregister_callback);
        }
    }
};

} // namespace std::execution::__forge_parsched_detail
