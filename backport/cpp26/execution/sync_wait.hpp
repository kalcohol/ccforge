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
#include "detail/value_result.hpp"
#include "env.hpp"
#include "run_loop.hpp"

#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace std::execution {

namespace __forge_sync_wait {

struct stopped_t {};

struct sync_wait_env {
    run_loop* loop_;

    friend auto tag_invoke(get_scheduler_t, const sync_wait_env& self) noexcept {
        return self.loop_->get_scheduler();
    }

    friend auto tag_invoke(get_start_scheduler_t, const sync_wait_env& self) noexcept {
        return self.loop_->get_scheduler();
    }

    friend auto tag_invoke(get_delegation_scheduler_t, const sync_wait_env& self) noexcept {
        return self.loop_->get_scheduler();
    }
};

template<class Value>
struct shared_state {
    using value_t = Value;
    std::variant<std::monostate, value_t, std::exception_ptr, stopped_t> result_;
};

template<class CompletionSignatures>
using value_t_for = __forge_meta::single_value_or_variant_t<CompletionSignatures>;

template<class State>
struct receiver {
    using receiver_concept = receiver_t;
    State* state_;
    run_loop* loop_;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        try {
            using value_t = typename State::value_t;
            using tuple_t = std::tuple<std::decay_t<Vs>...>;
            auto value = __forge_meta::value_from_tuple<value_t>(
                tuple_t{std::forward<Vs>(vs)...});
            state_->result_.template emplace<1>(std::move(value));
        } catch (...) {
            state_->result_.template emplace<2>(std::current_exception());
        }
        loop_->finish();
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
            state_->result_.template emplace<2>(std::forward<E>(e));
        } else {
            state_->result_.template emplace<2>(std::make_exception_ptr(std::forward<E>(e)));
        }
        loop_->finish();
    }

    void set_stopped() && noexcept {
        state_->result_.template emplace<3>();
        loop_->finish();
    }

    auto get_env() const noexcept -> sync_wait_env {
        return sync_wait_env{loop_};
    }
};

} // namespace __forge_sync_wait

} // namespace std::execution

namespace std::this_thread {

template<class S>
    requires std::execution::sender_in<
        S, std::execution::__forge_sync_wait::sync_wait_env>
auto sync_wait(S&& sndr) {
    using env_t = std::execution::__forge_sync_wait::sync_wait_env;
    using cs_t = std::execution::completion_signatures_of_t<S, env_t>;
    using value_t = std::execution::__forge_sync_wait::value_t_for<cs_t>;
    using state_t = std::execution::__forge_sync_wait::shared_state<value_t>;
    using recv_t = std::execution::__forge_sync_wait::receiver<state_t>;

    std::execution::run_loop loop;
    state_t state;
    auto op = std::execution::connect(std::forward<S>(sndr), recv_t{&state, &loop});
    std::execution::start(op);
    loop.run();

    if (state.result_.index() == 2) {
        std::rethrow_exception(std::get<2>(state.result_));
    }
    if (state.result_.index() == 3) {
        return std::optional<typename state_t::value_t>{std::nullopt};
    }
    return std::optional<typename state_t::value_t>{std::move(std::get<1>(state.result_))};
}

} // namespace std::this_thread

namespace std::execution {

using std::this_thread::sync_wait;

} // namespace std::execution
