#pragma once

#include <execution>
#include <optional>
#include <semaphore>

namespace forge_test {

// Completion releases the borrowed source, but leaves the operation alive.
// Strict stop mode diagnoses any registration subsequently torn down by it.
struct stop_completion_probe {
    std::optional<std::inplace_stop_source> source{std::in_place};
    std::binary_semaphore completed{0};

    struct receiver {
        using receiver_concept = std::execution::receiver_t;
        stop_completion_probe* probe;
        std::inplace_stop_token token;
        void finish() noexcept {
            auto* state = probe;
            state->source.reset();
            state->completed.release();
        }
        template<class... Ts> void set_value(Ts&&...) && noexcept { finish(); }
        template<class E> void set_error(E&&) && noexcept { finish(); }
        void set_stopped() && noexcept { finish(); }
        auto get_env() const noexcept {
            return std::execution::prop(std::execution::get_stop_token, token);
        }
    };

    auto get_receiver() -> receiver { return {this, source->get_token()}; }
    void wait() { completed.acquire(); }
};

} // namespace forge_test
