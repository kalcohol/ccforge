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
#include "run_loop.hpp"

namespace std::execution {

namespace __forge_sync_wait {

struct stopped_t {};

template<class... Vs>
struct shared_state {
    using value_t = std::tuple<Vs...>;
    std::variant<std::monostate, value_t, std::exception_ptr, stopped_t> result_;
    std::inplace_stop_source stop_source_;
};

template<class Tuple>
struct state_from_tuple;

template<class... Vs>
struct state_from_tuple<std::tuple<Vs...>> {
    using type = shared_state<Vs...>;
};

template<class CompletionSignatures>
struct value_tuple_for;

template<class... Sigs>
struct value_tuple_for<completion_signatures<Sigs...>> {
private:
    template<class Sig>
    struct is_value_sig : std::false_type {};
    template<class... Vs>
    struct is_value_sig<set_value_t(Vs...)> : std::true_type {};

    static constexpr std::size_t kCount = (static_cast<std::size_t>(is_value_sig<Sigs>::value) + ... + 0u);

    template<class Sig>
    struct value_tuple_of_sig {
        using type = std::tuple<>;
    };
    template<class... Vs>
    struct value_tuple_of_sig<set_value_t(Vs...)> {
        using type = std::tuple<Vs...>;
    };

    template<class... Ts>
    struct first_value_tuple_from_pack {
        using type = std::tuple<>;
    };

    template<class Sig, class... Rest>
    struct first_value_tuple_from_pack<Sig, Rest...> {
        using type = std::conditional_t<is_value_sig<Sig>::value,
                                        typename value_tuple_of_sig<Sig>::type,
                                        typename first_value_tuple_from_pack<Rest...>::type>;
    };

public:
    static_assert(kCount <= 1, "sync_wait MVP supports at most one set_value signature");
    using type = typename first_value_tuple_from_pack<Sigs...>::type;
};

template<class State>
struct receiver {
    using receiver_concept = receiver_t;
    State* state_;
    run_loop* loop_;

    template<class... Vs>
    friend void tag_invoke(set_value_t, receiver&& self, Vs&&... vs) noexcept {
        self.state_->result_.template emplace<1>(std::forward<Vs>(vs)...);
        self.loop_->finish();
    }

    template<class E>
    friend void tag_invoke(set_error_t, receiver&& self, E&& e) noexcept {
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
            self.state_->result_.template emplace<2>(std::forward<E>(e));
        } else {
            self.state_->result_.template emplace<2>(std::make_exception_ptr(std::forward<E>(e)));
        }
        self.loop_->finish();
    }

    friend void tag_invoke(set_stopped_t, receiver&& self) noexcept {
        self.state_->result_.template emplace<3>();
        self.loop_->finish();
    }

    friend auto tag_invoke(get_env_t, const receiver& self) noexcept {
        return std::execution::make_env(
            std::execution::make_prop(get_stop_token_t{}, self.state_->stop_source_.get_token()),
            std::execution::make_prop(get_scheduler_t{}, self.loop_->get_scheduler()));
    }
};

} // namespace __forge_sync_wait

} // namespace std::execution

namespace std::this_thread {

template<std::execution::sender_in S>
auto sync_wait(S&& sndr) {
    using cs_t = decltype(std::execution::get_completion_signatures(std::declval<S>(), std::execution::empty_env{}));
    using value_tuple_t = typename std::execution::__forge_sync_wait::value_tuple_for<cs_t>::type;
    using state_t = typename std::execution::__forge_sync_wait::state_from_tuple<value_tuple_t>::type;
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
    return std::optional<typename state_t::value_t>{std::get<1>(state.result_)};
}

} // namespace std::this_thread

namespace std::execution {

using std::this_thread::sync_wait;

} // namespace std::execution
