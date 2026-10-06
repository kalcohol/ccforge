// MIT License
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

#include <exception>
#include <execution>
#include <thread>
#include <utility>

namespace forge_test {

enum class completion_kind { value, error, stopped };

struct completion_agent_state {
    std::thread::id completed_on;
    std::thread::id resumed_on;
    int resumptions = 0;
};

struct early_completion_error {};

struct early_completion_sender {
    using sender_concept = std::execution::sender_t;

    completion_agent_state* state;
    completion_kind kind;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_stopped_t()> {
        return {};
    }

    template<class R>
    struct operation {
        using operation_state_concept = std::execution::operation_state_t;

        R receiver;
        completion_agent_state* state;
        completion_kind kind;

        void start() & noexcept {
            // The worker owns its receiver; the starting stack uses only
            // locals after publishing it and waits for completion to return.
            std::thread worker{[receiver = std::move(receiver),
                                state = state, kind = kind]() mutable {
                state->completed_on = std::this_thread::get_id();
                switch (kind) {
                case completion_kind::value:
                    std::execution::set_value(std::move(receiver));
                    break;
                case completion_kind::error:
                    std::execution::set_error(std::move(receiver),
                        std::make_exception_ptr(early_completion_error{}));
                    break;
                case completion_kind::stopped:
                    std::execution::set_stopped(std::move(receiver));
                    break;
                }
            }};
            worker.join();
        }
    };

    template<std::execution::receiver R>
    auto connect(R receiver) && -> operation<R> {
        return {std::move(receiver), state, kind};
    }
};

} // namespace forge_test
