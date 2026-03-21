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

namespace std::execution {

namespace __forge_detail {

// Non-movable mixin.  Inheriting this makes a type satisfy the
// operation_state requirement !move_constructible<O> while keeping
// the derived type an aggregate (C++17: base classes of aggregates
// need not have user-declared constructors as long as they have no
// virtual functions and no virtual/private/protected bases).
struct __immovable {
    __immovable() = default;
    __immovable(__immovable&&) = delete;
};

// tag_invoke protocol (internal).
namespace __tag_invoke {

void tag_invoke() = delete;

struct fn {
    template<class Tag, class... Args>
        requires requires(Tag&& tag, Args&&... args) {
            tag_invoke(static_cast<Tag&&>(tag), static_cast<Args&&>(args)...);
        }
    constexpr auto operator()(Tag&& tag, Args&&... args) const
        noexcept(noexcept(tag_invoke(static_cast<Tag&&>(tag), static_cast<Args&&>(args)...)))
            -> decltype(tag_invoke(static_cast<Tag&&>(tag), static_cast<Args&&>(args)...)) {
        return tag_invoke(static_cast<Tag&&>(tag), static_cast<Args&&>(args)...);
    }
};

} // namespace __tag_invoke

inline constexpr __tag_invoke::fn tag_invoke_fn{};

template<class Tag, class... Args>
concept tag_invocable =
    requires(Tag&& tag, Args&&... args) {
        std::execution::__forge_detail::tag_invoke_fn(
            static_cast<Tag&&>(tag), static_cast<Args&&>(args)...);
    };

template<class Tag, class... Args>
concept nothrow_tag_invocable =
    tag_invocable<Tag, Args...> &&
    noexcept(std::execution::__forge_detail::tag_invoke_fn(
        std::declval<Tag>(), std::declval<Args>()...));

template<class Tag, class... Args>
    requires tag_invocable<Tag, Args...>
using tag_invoke_result_t =
    decltype(std::execution::__forge_detail::tag_invoke_fn(
        std::declval<Tag>(), std::declval<Args>()...));

} // namespace __forge_detail

} // namespace std::execution
