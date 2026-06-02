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

#include <execution>
#include <atomic>
#include <exception>
#include <type_traits>
#include <utility>

namespace forge {

namespace __start_detached {

struct __detached_recv {
    using receiver_concept = std::execution::receiver_t;

    struct __state_base {
        void add_ref() noexcept {
            refs_.fetch_add(1, std::memory_order_relaxed);
        }

        void release() noexcept {
            if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete this;
            }
        }

        virtual void start() noexcept = 0;
        virtual ~__state_base() = default;

    private:
        std::atomic<unsigned> refs_{1};
    };

    __state_base* state;

    template<class... Vs>
    void set_value(Vs&&...) && noexcept {
        state->release();
    }

    template<class E>
    void set_error(E&&) && noexcept {
        std::terminate();
    }

    void set_stopped() && noexcept {
        state->release();
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

template<class S>
struct __state : __detached_recv::__state_base {
    using op_t = std::execution::connect_result_t<S, __detached_recv>;

    op_t op;

    explicit __state(S sndr)
        : op(std::execution::connect(std::move(sndr), __detached_recv{this}))
    {}

    void start() noexcept override {
        std::execution::start(op);
    }
};

} // namespace __start_detached

template<std::execution::sender S>
void start_detached(S&& sndr) {
    using source_t = std::decay_t<S>;
    using state_t = __start_detached::__state<source_t>;
    auto* state = new state_t(static_cast<S&&>(sndr));
    state->add_ref();
    state->start();
    state->release();
}

} // namespace forge
