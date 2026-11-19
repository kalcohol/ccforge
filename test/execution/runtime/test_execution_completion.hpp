#pragma once

#include <execution>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace forge_test {

template<class Error>
struct observed_completion {
    std::execution::run_loop loop;
    std::optional<Error> error;
    bool stopped = false;
};

template<class Error>
struct completion_receiver {
    using receiver_concept = std::execution::receiver_tag;
    observed_completion<Error>* state;

    template<class... Values>
    void set_value(Values&&...) && noexcept { state->loop.finish(); }

    template<class E>
    void set_error(E&& error) && noexcept {
        if constexpr (std::is_same_v<std::remove_cvref_t<E>, Error>) {
            state->error.emplace(std::forward<E>(error));
        }
        state->loop.finish();
    }

    void set_stopped() && noexcept {
        state->stopped = true;
        state->loop.finish();
    }
};

template<class Error = std::exception_ptr, class Sender>
auto observe_completion(Sender&& sender) {
    observed_completion<Error> state;
    auto op = std::execution::connect(
        std::forward<Sender>(sender), completion_receiver<Error>{&state});
    std::execution::start(op);
    state.loop.run();
    struct result {
        std::optional<Error> error;
        bool stopped;
    };
    return result{std::move(state.error), state.stopped};
}

} // namespace forge_test
