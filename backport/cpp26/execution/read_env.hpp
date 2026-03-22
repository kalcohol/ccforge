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

namespace std::execution {

namespace __forge_read_env {

template<class Tag, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    R __rcvr;
    Tag __tag;

    __op(Tag t, R r) : __rcvr(std::move(r)), __tag(std::move(t)) {}

    friend void tag_invoke(start_t, __op& self) noexcept {
        set_value(std::move(self.__rcvr),
                  self.__tag(std::execution::get_env(self.__rcvr)));
    }
};

template<class Tag>
struct __sender {
    using sender_concept = sender_t;
    Tag __tag;

    template<class Env>
    friend auto tag_invoke(get_completion_signatures_t,
                           const __sender& self,
                           Env&&) noexcept {
        using result_t = std::invoke_result_t<Tag, Env>;
        return completion_signatures<set_value_t(result_t)>{};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, __sender self, R r) {
        return __op<Tag, R>{std::move(self.__tag), std::move(r)};
    }

    friend auto tag_invoke(get_env_t, const __sender&) noexcept -> empty_env {
        return {};
    }
};

} // namespace __forge_read_env

template<class Tag>
[[nodiscard]] auto read_env(Tag tag) {
    return __forge_read_env::__sender<Tag>{std::move(tag)};
}

} // namespace std::execution
