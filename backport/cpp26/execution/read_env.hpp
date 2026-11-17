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
#include "detail/adaptor.hpp"

#include <exception>

namespace std::execution {

namespace __forge_read_env {

// Unevaluated stand-in for get_env: preserve prvalues as well as references.
template<class Env>
Env __environment() noexcept;

template<class Tag, class Env>
concept __valid_query = requires(Env& env, Tag& query) {
    Tag()(env);
    requires (!std::is_void_v<decltype(Tag()(env))>);
    query(__environment<Env>());
    requires (!std::is_void_v<decltype(query(__environment<Env>()))>);
};

template<class Tag, class Env>
    requires __valid_query<Tag, Env>
using __cs = std::conditional_t<noexcept(std::declval<Tag&>()(__environment<Env>())),
    completion_signatures<set_value_t(decltype(std::declval<Tag&>()(__environment<Env>())))>,
    completion_signatures<set_value_t(decltype(std::declval<Tag&>()(__environment<Env>()))),
                          set_error_t(std::exception_ptr)>>;

template<class Tag, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    R __rcvr;
    Tag __tag;

    template<class Data>
    __op(Data&& t, R r)
        noexcept(std::is_nothrow_move_constructible_v<R> && noexcept(Tag(
            __forge_detail::__allocator_aware_forward(static_cast<Data&&>(t), r))))
        : __rcvr(std::move(r)), __tag(__forge_detail::__allocator_aware_forward(
              static_cast<Data&&>(t), __rcvr)) {}

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
    auto get_completion_signatures(Env&&) const noexcept -> __cs<Tag, Env> { return {}; }

    template<receiver R>
        requires __valid_query<Tag, env_of_t<R>>
    auto connect(R r) &&
        noexcept(noexcept(__op<Tag, R>{std::move(__tag), std::move(r)})) -> __op<Tag, R> {
        return __op<Tag, R>{std::move(__tag), std::move(r)};
    }

    template<receiver R>
        requires std::copy_constructible<Tag> && __valid_query<Tag, env_of_t<R>>
    auto connect(R r) const&
        noexcept(noexcept(__op<Tag, R>{__tag, std::move(r)})) -> __op<Tag, R> {
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
