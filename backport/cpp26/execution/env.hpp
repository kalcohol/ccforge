// MIT License
//
// Copyright (c) 2026 Forge Project
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

#include "detail.hpp"

namespace std::execution {

struct empty_env {};

struct get_env_t {
    template<class T>
        requires __forge_detail::tag_invocable<get_env_t, const T&>
    auto operator()(const T& obj) const
        noexcept(__forge_detail::nothrow_tag_invocable<get_env_t, const T&>)
            -> __forge_detail::tag_invoke_result_t<get_env_t, const T&> {
        return __forge_detail::tag_invoke_fn(*this, obj);
    }
};
inline constexpr get_env_t get_env{};

template<class T>
using env_of_t = decltype(std::execution::get_env(std::declval<const T&>()));

struct get_scheduler_t {
    template<class Env>
        requires __forge_detail::tag_invocable<get_scheduler_t, Env>
    auto operator()(Env&& env) const
        noexcept(__forge_detail::nothrow_tag_invocable<get_scheduler_t, Env>)
            -> __forge_detail::tag_invoke_result_t<get_scheduler_t, Env> {
        return __forge_detail::tag_invoke_fn(*this, static_cast<Env&&>(env));
    }
};
inline constexpr get_scheduler_t get_scheduler{};

struct get_stop_token_t {
    template<class Env>
        requires __forge_detail::tag_invocable<get_stop_token_t, Env>
    auto operator()(Env&& env) const
        noexcept(__forge_detail::nothrow_tag_invocable<get_stop_token_t, Env>)
            -> __forge_detail::tag_invoke_result_t<get_stop_token_t, Env> {
        return __forge_detail::tag_invoke_fn(*this, static_cast<Env&&>(env));
    }

    // Fallback: return never_stop_token.
    std::never_stop_token operator()(const empty_env&) const noexcept { return {}; }
};
inline constexpr get_stop_token_t get_stop_token{};

struct get_allocator_t {
    template<class Env>
        requires __forge_detail::tag_invocable<get_allocator_t, Env>
    auto operator()(Env&& env) const
        noexcept(__forge_detail::nothrow_tag_invocable<get_allocator_t, Env>)
            -> __forge_detail::tag_invoke_result_t<get_allocator_t, Env> {
        return __forge_detail::tag_invoke_fn(*this, static_cast<Env&&>(env));
    }
};
inline constexpr get_allocator_t get_allocator{};


template<class Tag, class Value>
struct prop {
    [[no_unique_address]] Tag tag_;
    [[no_unique_address]] Value value_;

    friend auto tag_invoke(Tag, const prop& self)
        noexcept(std::is_nothrow_copy_constructible_v<Value>) -> Value {
        return self.value_;
    }
};

template<class Tag, class Value>
prop(Tag, Value) -> prop<Tag, Value>;

template<class Tag, class Value>
[[nodiscard]] auto make_prop(Tag tag, Value&& val) {
    return prop<Tag, std::decay_t<Value>>{tag, std::forward<Value>(val)};
}

template<class... Envs>
struct env : Envs... {};

template<class... Envs>
env(Envs...) -> env<Envs...>;

template<class... Envs>
[[nodiscard]] auto make_env(Envs&&... envs) {
    return env<std::decay_t<Envs>...>{std::forward<Envs>(envs)...};
}

// get_completion_scheduler CPO — [exec.getcomplsched]
template<class CPO>
struct get_completion_scheduler_t {
    template<class Env>
        requires __forge_detail::tag_invocable<get_completion_scheduler_t<CPO>, Env>
    auto operator()(Env&& env) const
        noexcept(__forge_detail::nothrow_tag_invocable<get_completion_scheduler_t<CPO>, Env>)
            -> __forge_detail::tag_invoke_result_t<get_completion_scheduler_t<CPO>, Env> {
        return __forge_detail::tag_invoke_fn(*this, static_cast<Env&&>(env));
    }
};
template<class CPO>
inline constexpr get_completion_scheduler_t<CPO> get_completion_scheduler{};

} // namespace std::execution
