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

// Protocol model: formal/sync_wait; every edit, comments included, requires a source-lock refresh.

#include "concepts.hpp"
#include "detail/value_result.hpp"
#include "env.hpp"
#include "run_loop.hpp"

#include <exception>
#include <optional>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_sync_wait {

struct sync_wait_env {
    run_loop* loop_;

    auto query(get_scheduler_t) const noexcept {
        return this->loop_->get_scheduler();
    }

    auto query(get_start_scheduler_t) const noexcept {
        return this->loop_->get_scheduler();
    }

    auto query(get_delegation_scheduler_t) const noexcept {
        return this->loop_->get_scheduler();
    }
};

template<class Value>
struct shared_state {
    run_loop loop;
    std::exception_ptr error;
    std::optional<Value> result;
};

template<class Value>
struct receiver {
    using receiver_concept = receiver_tag;
    shared_state<Value>* state_;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        try {
            state_->result.emplace(std::forward<Vs>(vs)...);
        } catch (...) {
            state_->error = std::current_exception();
        }
        state_->loop.finish();
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
            state_->error = std::forward<E>(e);
        } else if constexpr (std::is_same_v<std::decay_t<E>, std::error_code>) {
            state_->error = std::make_exception_ptr(std::system_error(std::forward<E>(e)));
        } else {
            state_->error = std::make_exception_ptr(std::forward<E>(e));
        }
        state_->loop.finish();
    }

    void set_stopped() && noexcept {
        state_->loop.finish();
    }

    auto get_env() const noexcept -> sync_wait_env {
        return sync_wait_env{&state_->loop};
    }
};

struct __driver {
    template<class Value, class S>
        requires __forge_sender_to<S, receiver<Value>>
    static auto __run(S&& sndr) -> std::optional<Value> {
        shared_state<Value> state;
        auto op = std::execution::connect(
            std::forward<S>(sndr), receiver<Value>{&state});
        std::execution::start(op);
        state.loop.run();

        if (state.error) {
            std::rethrow_exception(std::move(state.error));
        }
        return std::move(state.result);
    }
};

} // namespace __forge_sync_wait

} // namespace std::execution

namespace std::this_thread {

struct sync_wait_t {
    template<class S>
    auto operator()(S&& sndr) const {
        namespace ex = std::execution;
        using env_t = ex::__forge_sync_wait::sync_wait_env;

        if constexpr (!ex::sender_in<S, env_t>) {
            static_assert(ex::sender_in<S, env_t>,
                          "sync_wait requires a sender in its run_loop environment");
        } else {
            using cs_t = ex::completion_signatures_of_t<S, env_t>;
            constexpr auto count = ex::__forge_meta::__value_count<cs_t>;
            static_assert(count == 1,
                          "sync_wait requires exactly one value completion signature");
            if constexpr (count == 1) {
                using result_t = std::optional<ex::__forge_meta::__exact_value_tuple_t<cs_t>>;
                using domain_t = ex::__forge_domain::__compl_domain_t<ex::set_value_t, S, env_t>;
                using applied_t = decltype(ex::apply_sender(
                    domain_t{}, *this, std::forward<S>(sndr)));
                static_assert(std::same_as<applied_t, result_t>,
                              "sync_wait domain must return its prescribed optional tuple");
                return ex::apply_sender(domain_t{}, *this, std::forward<S>(sndr));
            }
        }
    }

    template<class S>
        requires requires {
            std::execution::__forge_sync_wait::__driver::__run<
                std::execution::__forge_meta::__exact_value_tuple_t<
                    std::execution::completion_signatures_of_t<S,
                        std::execution::__forge_sync_wait::sync_wait_env>>>(std::declval<S>());
        }
    auto apply_sender(S&& sndr) const {
        using cs_t = std::execution::completion_signatures_of_t<S,
            std::execution::__forge_sync_wait::sync_wait_env>;
        using value_t = std::execution::__forge_meta::__exact_value_tuple_t<cs_t>;
        return std::execution::__forge_sync_wait::__driver::__run<value_t>(std::forward<S>(sndr));
    }
};

inline constexpr sync_wait_t sync_wait{};

} // namespace std::this_thread
