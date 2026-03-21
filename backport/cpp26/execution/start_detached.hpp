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

#include "concepts.hpp"
#include "env.hpp"

#include <cstdlib>

namespace std::execution {

namespace __forge_start_detached {

struct __detached_recv {
    using receiver_concept = receiver_t;

    struct __state_base {
        virtual void destroy() noexcept = 0;
        virtual ~__state_base() = default;
    };

    __state_base* __state;

    template<class... Vs>
    friend void tag_invoke(set_value_t, __detached_recv&& self, Vs&&...) noexcept {
        self.__state->destroy();
    }

    template<class E>
    friend void tag_invoke(set_error_t, __detached_recv&&, E&&) noexcept {
        std::terminate();
    }

    friend void tag_invoke(set_stopped_t, __detached_recv&& self) noexcept {
        self.__state->destroy();
    }

    friend auto tag_invoke(get_env_t, const __detached_recv&) noexcept
        -> empty_env {
        return {};
    }
};

template<class S>
struct __state : __detached_recv::__state_base {
    using __op_t = connect_result_t<S, __detached_recv>;
    __op_t __op;

    explicit __state(S sndr)
        : __op(std::execution::connect(std::move(sndr), __detached_recv{this}))
    {}

    void destroy() noexcept override {
        delete this;
    }
};

} // namespace __forge_start_detached

template<sender S>
void start_detached(S sndr) {
    using state_t = __forge_start_detached::__state<S>;
    auto* s = new state_t(std::move(sndr));
    std::execution::start(s->__op);
}

} // namespace std::execution
