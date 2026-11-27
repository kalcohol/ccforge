// Shared fixtures of the std::execution::task environment tests (pilot G2).
// Standard-owned: only <execution> and the standard library.

#pragma once

#include <execution>

#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace task_environment_test {

namespace ex = std::execution;

// Lets a coroutine look at its own promise environment. With an inline start
// scheduler await_transform hands the sender's own awaitable straight back.
template<class Inspect>
struct inspect_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept {
        return ex::completion_signatures<ex::set_value_t()>{};
    }

    template<class Promise>
    struct awaiter {
        Inspect inspect;
        Promise* promise;

        bool await_ready() const noexcept { return true; }
        void await_suspend(std::coroutine_handle<>) const noexcept {}
        decltype(auto) await_resume() { return inspect(ex::get_env(std::as_const(*promise))); }
    };

    template<class Promise>
    auto as_awaitable(Promise& promise) && -> awaiter<Promise> {
        return {std::move(inspect), std::addressof(promise)};
    }

    Inspect inspect;
};

template<class Inspect>
inspect_sender(Inspect) -> inspect_sender<Inspect>;

// What an operation delivered. Every terminal is counted, whatever its
// channel, so a second delivery cannot pass unnoticed.
struct outcome;

// A receiver whose environment is built by a callable, so each case decides
// what the environment is and whether it is a prvalue or a reference. The
// hook runs last in every terminal and may destroy the operation.
template<class MakeEnv>
struct probe_receiver {
    using receiver_concept = ex::receiver_tag;

    decltype(auto) get_env() const noexcept { return make_env(); }

    template<class... Values>
    void set_value(Values&&... values) && noexcept;
    template<class Error>
    void set_error(Error&&) && noexcept;
    void set_stopped() && noexcept;

    MakeEnv make_env;
    outcome* out;
};

struct outcome {
    std::optional<int> result;
    std::string terminal;
    int terminals = 0;
    std::function<void()> at_terminal;

    template<class MakeEnv>
    auto receiver(MakeEnv make_env, std::function<void()> hook = {}) {
        at_terminal = std::move(hook);
        return probe_receiver<MakeEnv>{std::move(make_env), this};
    }

    void entered(const char* channel) {
        ++terminals;
        terminal = channel;
        if (at_terminal) at_terminal();
    }
};

template<class MakeEnv>
template<class... Values>
void probe_receiver<MakeEnv>::set_value(Values&&... values) && noexcept {
    outcome* const target = out;
    target->result.emplace(std::forward<Values>(values)...);
    target->entered("");
}

template<class MakeEnv>
template<class Error>
void probe_receiver<MakeEnv>::set_error(Error&&) && noexcept {
    out->entered("error");
}

template<class MakeEnv>
void probe_receiver<MakeEnv>::set_stopped() && noexcept {
    out->entered("stopped");
}

struct inline_environment {
    using start_scheduler_type = ex::inline_scheduler;
};

// A sender that completes when the harness says so.
struct gate {
    std::function<void()> open;
};

template<class Receiver>
struct gate_operation {
    using operation_state_concept = ex::operation_state_tag;

    gate_operation(gate* target, Receiver receiver) : target(target), receiver(std::move(receiver)) {}
    gate_operation(gate_operation&&) = delete;

    void start() & noexcept {
        target->open = [this] { ex::set_value(std::move(receiver)); };
    }

    gate* target;
    Receiver receiver;
};

struct gate_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept {
        return ex::completion_signatures<ex::set_value_t()>{};
    }

    template<ex::receiver Receiver>
    auto connect(Receiver receiver) const -> gate_operation<Receiver> { return {target, std::move(receiver)}; }

    gate* target;
};
} // namespace task_environment_test
