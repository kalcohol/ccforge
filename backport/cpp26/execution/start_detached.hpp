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

#include <atomic>
#include <cstdlib>

namespace std::execution {

namespace __forge_start_detached {

// Forge's detached receiver deliberately terminates on set_error. This keeps
// the fire-and-forget lifetime model simple: value/stopped release the shared
// state, while error is treated as an unhandled asynchronous failure.

struct __detached_recv {
    using receiver_concept = receiver_t;

    struct __state_base {
        void add_ref() noexcept {
            __refs.fetch_add(1, std::memory_order_relaxed);
        }

        void release() noexcept {
            if (__refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete this;
            }
        }

        virtual void start() noexcept = 0;
        virtual ~__state_base() = default;

    private:
        std::atomic<unsigned> __refs{1};
    };

    __state_base* __state;

    template<class... Vs>
    void set_value(Vs&&...) && noexcept {
        __state->release();
    }

    template<class E>
    void set_error(E&&) && noexcept {
        std::terminate();
    }

    void set_stopped() && noexcept {
        __state->release();
    }

    auto get_env() const noexcept -> empty_env {
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

    void start() noexcept override {
        std::execution::start(__op);
    }
};

} // namespace __forge_start_detached

template<sender S>
void start_detached(S sndr) {
    using state_t = __forge_start_detached::__state<S>;
    auto* s = new state_t(std::move(sndr));
    s->add_ref();
    s->start();
    s->release();
}

} // namespace std::execution
