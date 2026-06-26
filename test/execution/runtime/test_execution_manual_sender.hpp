#pragma once

#include <execution>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace forge_execution_test {

struct manual_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool stop_requested = false;
    bool completed = false;
    int stop_completion_attempts = 0;
    std::function<void(int)> complete_value;
};

template<class R>
struct manual_op {
    using operation_state_concept = std::execution::operation_state_t;

    struct stop_callback {
        manual_op* self;

        void operator()() noexcept {
            self->complete_stopped();
        }
    };

    using env_t = std::execution::env_of_t<R>;
    using token_t = decltype(std::execution::get_stop_token(std::declval<env_t>()));
    using callback_t = std::stop_callback_for_t<token_t, stop_callback>;

    R rcvr;
    std::shared_ptr<manual_state> state;
    std::optional<callback_t> callback;
    std::atomic<bool> done{false};

    void start() & noexcept {
        auto token = std::execution::get_stop_token(std::execution::get_env(rcvr));
        {
            std::lock_guard lk{state->mtx};
            state->started = true;
            state->complete_value = [this](int value) noexcept {
                complete_value(value);
            };
        }
        state->cv.notify_all();

        if (token.stop_possible()) {
            callback.emplace(token, stop_callback{this});
        }
    }

    void complete_value(int value) noexcept {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        {
            std::lock_guard lk{state->mtx};
            state->completed = true;
            state->complete_value = {};
        }
        state->cv.notify_all();
        callback.reset();
        std::execution::set_value(std::move(rcvr), value);
    }

    void complete_stopped() noexcept {
        {
            std::lock_guard lk{state->mtx};
            ++state->stop_completion_attempts;
        }
        if (done.exchange(true, std::memory_order_acq_rel)) {
            state->cv.notify_all();
            return;
        }
        {
            std::lock_guard lk{state->mtx};
            state->stop_requested = true;
            state->completed = true;
            state->complete_value = {};
        }
        state->cv.notify_all();
        std::execution::set_stopped(std::move(rcvr));
    }
};

struct manual_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<manual_state> state;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R r) && -> manual_op<R> {
        return manual_op<R>{std::move(r), std::move(state)};
    }

    template<std::execution::receiver R>
    auto connect(R r) const& -> manual_op<R> {
        return manual_op<R>{std::move(r), state};
    }
};

inline bool wait_until_started(const std::shared_ptr<manual_state>& state) {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, std::chrono::seconds(2), [&] {
        return state->started;
    });
}

inline bool wait_until_completed(const std::shared_ptr<manual_state>& state) {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, std::chrono::seconds(2), [&] {
        return state->completed;
    });
}

inline bool wait_until_stop_requested(const std::shared_ptr<manual_state>& state) {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, std::chrono::seconds(2), [&] {
        return state->stop_requested;
    });
}

inline auto manual_value_completer(const std::shared_ptr<manual_state>& state)
    -> std::function<void(int)> {
    std::function<void(int)> complete;
    {
        std::lock_guard lk{state->mtx};
        complete = state->complete_value;
    }
    return complete;
}

} // namespace forge_execution_test
