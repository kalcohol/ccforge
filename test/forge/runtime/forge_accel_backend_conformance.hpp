#pragma once

#include <forge/accel.hpp>

#include <chrono>
#include <condition_variable>
#include <execution>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>

namespace forge_test::accel_conformance {

using namespace std::chrono_literals;

struct completion_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool value = false;
    bool stopped = false;
    std::exception_ptr error;

    [[nodiscard]] auto done() const noexcept -> bool {
        return value || stopped || error;
    }
};

struct completion_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<completion_state> state;

    void set_value() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
        }
        state->cv.notify_all();
    }

    void set_error(std::exception_ptr error) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->error = std::move(error);
        }
        state->cv.notify_all();
    }

    void set_stopped() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->stopped = true;
        }
        state->cv.notify_all();
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

[[nodiscard]] inline auto wait_done(
    const std::shared_ptr<completion_state>& state,
    std::chrono::milliseconds timeout = 2s) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, timeout, [&] { return state->done(); });
}

struct blocking_gate {
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;

    void mark_started_and_wait() {
        {
            std::lock_guard lk{mtx};
            started = true;
        }
        cv.notify_all();

        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return release; });
    }

    [[nodiscard]] auto wait_started(
        std::chrono::milliseconds timeout = 2s) -> bool {
        std::unique_lock lk{mtx};
        return cv.wait_for(lk, timeout, [&] { return started; });
    }

    void release_gate() {
        {
            std::lock_guard lk{mtx};
            release = true;
        }
        cv.notify_all();
    }
};

template<class Sender>
[[nodiscard]] auto sync_ok(Sender&& sender) -> bool {
    return std::execution::sync_wait(std::forward<Sender>(sender)).has_value();
}

template<class Op>
struct connected_operation {
    Op op;
    std::shared_ptr<completion_state> state;
};

template<class Sender>
[[nodiscard]] auto connect_async(Sender&& sender) {
    auto state = std::make_shared<completion_state>();
    return connected_operation<
        decltype(std::execution::connect(
            std::forward<Sender>(sender),
            completion_receiver{state}))>{
        std::execution::connect(
            std::forward<Sender>(sender),
            completion_receiver{state}),
        std::move(state)};
}

struct mock_backend_adapter {
    using context_options = forge::accel::mock::context_options;
    using context = forge::accel::mock::context;
    using queue = forge::accel::mock::queue;
    using device = forge::accel::mock::device;
    using device_session = forge::accel::mock::device_session;
    using event = forge::accel::mock::event;
    using trace_sink = forge::accel::mock::trace_sink;
    using blocking_gate = ::forge_test::accel_conformance::blocking_gate;

    template<class T>
    using host_buffer = forge::accel::mock::host_buffer<T>;

    template<class T>
    using device_buffer = forge::accel::mock::device_buffer<T>;

    [[nodiscard]] static auto make_context(context_options options = {}) -> context {
        return context{options};
    }

    [[nodiscard]] static auto get_queue(
        context& ctx,
        forge::accel::queue_kind kind = forge::accel::queue_kind::general)
        -> queue {
        return ctx.get_queue(kind);
    }

    [[nodiscard]] static auto get_device(context& ctx) -> device {
        return ctx.get_device();
    }

    [[nodiscard]] static auto get_device_queue(
        device dev,
        forge::accel::queue_kind kind = forge::accel::queue_kind::general)
        -> queue {
        return dev.get_queue(kind);
    }

    [[nodiscard]] static auto open_session(device dev) -> device_session {
        return dev.open_session();
    }

    template<class Sender>
    [[nodiscard]] static auto sync_ok(Sender&& sender) -> bool {
        return ::forge_test::accel_conformance::sync_ok(
            std::forward<Sender>(sender));
    }

    template<class Sender>
    [[nodiscard]] static auto connect_async(Sender&& sender) {
        return ::forge_test::accel_conformance::connect_async(
            std::forward<Sender>(sender));
    }

    [[nodiscard]] static auto wait_done(
        const std::shared_ptr<completion_state>& state,
        std::chrono::milliseconds timeout = 2s) -> bool {
        return ::forge_test::accel_conformance::wait_done(state, timeout);
    }

    template<class T>
    [[nodiscard]] static auto make_host_buffer(
        context& ctx,
        std::size_t size,
        forge::accel::memory_kind kind = forge::accel::memory_kind::host)
        -> host_buffer<T> {
        return host_buffer<T>{ctx, size, kind};
    }

    template<class T>
    [[nodiscard]] static auto make_device_buffer(
        context& ctx,
        std::size_t size,
        forge::accel::memory_kind kind = forge::accel::memory_kind::device)
        -> device_buffer<T> {
        return device_buffer<T>{ctx, size, kind};
    }

    template<class T>
    [[nodiscard]] static auto copy_to_device(
        queue& q,
        device_buffer<T>& dst,
        std::span<const T> src) {
        return forge::accel::mock::copy_to_device(q, dst, src);
    }

    template<class T>
    [[nodiscard]] static auto copy_to_host(
        queue& q,
        std::span<T> dst,
        device_buffer<T>& src) {
        return forge::accel::mock::copy_to_host(q, dst, src);
    }

    template<class T>
    [[nodiscard]] static auto copy_device_to_device(
        queue& q,
        device_buffer<T>& dst,
        device_buffer<T>& src) {
        return forge::accel::mock::copy_device_to_device(q, dst, src);
    }

    template<class T>
    [[nodiscard]] static auto flush(queue& q, device_buffer<T>& buffer) {
        return forge::accel::mock::flush(q, buffer);
    }

    template<class T>
    [[nodiscard]] static auto invalidate(queue& q, device_buffer<T>& buffer) {
        return forge::accel::mock::invalidate(q, buffer);
    }

    template<class Fn>
    [[nodiscard]] static auto submit(queue& q, Fn&& fn) {
        return forge::accel::mock::submit(q, std::forward<Fn>(fn));
    }

    template<class Fn>
    [[nodiscard]] static auto submit(device_session& session, Fn&& fn) {
        return forge::accel::mock::submit(session, std::forward<Fn>(fn));
    }

    template<class Fn>
    [[nodiscard]] static auto submit_typed(queue& q, Fn&& fn) {
        return forge::accel::mock::submit_typed(
            q,
            std::forward<Fn>(fn));
    }

    [[nodiscard]] static auto make_event() -> event {
        return event{};
    }

    [[nodiscard]] static auto record_event(queue& q, event& ev) {
        return forge::accel::mock::record_event(q, ev);
    }

    [[nodiscard]] static auto wait_event(queue& q, event& ev) {
        return forge::accel::mock::wait_event(q, ev);
    }

    [[nodiscard]] static auto wait_event(
        queue& q,
        event& ev,
        forge::accel::mock::event_wait_options options) {
        return forge::accel::mock::wait_event(q, ev, options);
    }

    [[nodiscard]] static auto synchronize_event(queue& q, event& ev) {
        return forge::accel::mock::synchronize_event(q, ev);
    }

    [[nodiscard]] static auto fence(queue& q) {
        return forge::accel::mock::fence(q);
    }

    [[nodiscard]] static auto query_event(event& ev) {
        return forge::accel::mock::query_event(ev);
    }

    template<class Request, class Response, class Handler>
    [[nodiscard]] static auto submit_packet(
        device_session& session,
        forge::accel::mock::command_packet<Request, Response> packet,
        Handler&& handler,
        forge::accel::mock::command_options options = {}) {
        return forge::accel::mock::submit_packet(
            session,
            std::move(packet),
            std::forward<Handler>(handler),
            options);
    }

    template<class Request, class Response, class Handler>
    [[nodiscard]] static auto submit_packet_typed(
        device_session& session,
        forge::accel::mock::command_packet<Request, Response> packet,
        Handler&& handler,
        forge::accel::mock::command_options options = {}) {
        return forge::accel::mock::submit_packet_typed(
            session,
            std::move(packet),
            std::forward<Handler>(handler),
            options);
    }
};

} // namespace forge_test::accel_conformance
