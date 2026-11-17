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

#include <exception>

namespace std::execution {

namespace __forge_read_env {

// Unevaluated stand-in for get_env: preserve prvalues as well as references.
template<class Env>
Env __environment() noexcept;

template<class Tag, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    R __rcvr;
    Tag __tag;

    __op(Tag t, R r) : __rcvr(std::move(r)), __tag(std::move(t)) {}

    void start() & noexcept {
        if constexpr (noexcept(__tag(std::execution::get_env(__rcvr)))) {
            set_value(std::move(__rcvr),
                      __tag(std::execution::get_env(__rcvr)));
        } else {
            try {
                set_value(std::move(__rcvr),
                          __tag(std::execution::get_env(__rcvr)));
            } catch (...) {
                set_error(std::move(__rcvr), std::current_exception());
            }
        }
    }
};

template<class Tag>
struct __sender {
    using sender_concept = sender_t;
    using tag_t = Tag;

    Tag __tag;

    template<class Env>
        requires requires(Env& env, Tag& query) {
            Tag()(env);
            requires (!std::is_void_v<decltype(Tag()(env))>);
            query(__environment<Env>());
            requires (!std::is_void_v<decltype(query(__environment<Env>()))>);
        }
    auto get_completion_signatures(Env&&) const noexcept {
        // The check uses Q()(env); start invokes the stored query as an lvalue
        // with the receiver's exact environment value category.
        using result_t = decltype(std::declval<Tag&>()(__environment<Env>()));
        if constexpr (noexcept(std::declval<Tag&>()(__environment<Env>()))) {
            return completion_signatures<set_value_t(result_t)>{};
        } else {
            return completion_signatures<
                set_value_t(result_t),
                set_error_t(std::exception_ptr)>{};
        }
    }

    template<receiver R>
    auto connect(R r) && {
        return __op<Tag, R>{std::move(__tag), std::move(r)};
    }

    template<receiver R>
        requires std::copy_constructible<Tag>
    auto connect(R r) const& {
        return __op<Tag, R>{__tag, std::move(r)};
    }

    auto get_env() const noexcept -> __empty_env {
        return {};
    }
};

} // namespace __forge_read_env

struct read_env_t {
    template<class Tag>
    [[nodiscard]] constexpr auto operator()(Tag tag) const {
        return __forge_read_env::__sender<Tag>{std::move(tag)};
    }
};

inline constexpr read_env_t read_env{};

} // namespace std::execution
