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

// Internal sender/receiver machinery for forge::accel::mock::context.
// This header is included only after detail/state.hpp.

namespace forge::accel::mock::__detail {

template<class R, class Action>
struct __command_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<__state> state;
    std::shared_ptr<__queue_state> queue;
    std::shared_ptr<__session_state> session;
    std::optional<worker_generation> accepted_generation;
    R rcvr;
    Action action;

    void set_value() && noexcept {
        __current_state_guard guard{state.get()};
        try {
            state->record_trace(__make_trace_event(
                queue,
                session,
                trace_event_kind::started,
                accepted_generation));
            if (session && accepted_generation) {
                __validate_session_for_execution(
                    session,
                    *accepted_generation,
                    "forge::accel::mock command: session is not usable");
            } else {
                __validate_queue_for_execution(
                    queue,
                    accepted_generation,
                    "forge::accel::mock command: queue device is not usable");
            }
            std::invoke(std::move(action));
            state->record_trace(__make_trace_event(
                queue,
                session,
                trace_event_kind::completed,
                accepted_generation));
            state->finish_one();
            std::execution::set_value(std::move(rcvr));
        } catch (const __stopped_signal&) {
            state->record_trace(__make_trace_event(
                queue,
                session,
                trace_event_kind::stopped,
                accepted_generation));
            state->finish_one();
            std::execution::set_stopped(std::move(rcvr));
        } catch (...) {
            auto ep = std::current_exception();
            __record_trace_exception(
                state,
                queue,
                session,
                accepted_generation,
                command_id{},
                ep);
            state->finish_one();
            std::execution::set_error(std::move(rcvr), std::move(ep));
        }
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        __current_state_guard guard{state.get()};
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
            __record_trace_exception(
                state,
                queue,
                session,
                accepted_generation,
                command_id{},
                e);
        } else {
            auto event = __make_trace_event(
                queue,
                session,
                trace_event_kind::error,
                accepted_generation);
            event.error = error_kind::unknown;
            event.status = command_status::failed;
            state->record_trace(event);
        }
        state->finish_one();
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
            std::execution::set_error(std::move(rcvr), static_cast<E&&>(e));
        } else {
            std::execution::set_error(
                std::move(rcvr),
                std::make_exception_ptr(static_cast<E&&>(e)));
        }
    }

    void set_stopped() && noexcept {
        __current_state_guard guard{state.get()};
        state->record_trace(__make_trace_event(
            queue,
            session,
            trace_event_kind::stopped,
            accepted_generation));
        state->finish_one();
        std::execution::set_stopped(std::move(rcvr));
    }

    auto get_env() const noexcept(noexcept(std::execution::get_env(rcvr)))
        -> decltype(std::execution::get_env(rcvr)) {
        return std::execution::get_env(rcvr);
    }
};

template<class T>
class __op_box {
public:
    __op_box() noexcept = default;
    __op_box(const __op_box&) = delete;
    __op_box& operator=(const __op_box&) = delete;

    ~__op_box() noexcept {
        destroy();
    }

    template<class Factory>
    T* emplace_from(Factory&& factory) {
        destroy();
        auto* ptr = ::new (static_cast<void*>(storage_)) T(static_cast<Factory&&>(factory)());
        has_value_ = true;
        return ptr;
    }

    void destroy() noexcept {
        if (!has_value_) {
            return;
        }
        get().~T();
        has_value_ = false;
    }

    [[nodiscard]] T& get() noexcept {
        return *std::launder(reinterpret_cast<T*>(storage_));
    }

private:
    alignas(T) unsigned char storage_[sizeof(T)]{};
    bool has_value_ = false;
};

template<class Action>
struct __command_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__queue_state> queue;
    std::shared_ptr<__session_state> session;
    Action action;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> __void_completion_signatures {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
    struct __op {
        using operation_state_concept = std::execution::operation_state_t;
        using scheduler_t = strand::scheduler;
        using schedule_sender_t = decltype(std::execution::schedule(
            std::declval<scheduler_t>()));
        using receiver_t = __command_receiver<R, Action>;
        using op_t = std::execution::connect_result_t<schedule_sender_t, receiver_t>;

        __op(std::shared_ptr<__queue_state> queue, Action action, R rcvr)
            : queue_(std::move(queue))
            , action_(std::move(action))
            , rcvr_(std::move(rcvr))
        {}

        __op(
            std::shared_ptr<__queue_state> queue,
            std::shared_ptr<__session_state> session,
            Action action,
            R rcvr)
            : queue_(std::move(queue))
            , session_(std::move(session))
            , action_(std::move(action))
            , rcvr_(std::move(rcvr))
        {}

        __op(__op&&) = delete;
        __op& operator=(__op&&) = delete;
        __op(const __op&) = delete;
        __op& operator=(const __op&) = delete;

        void start() & noexcept {
            auto state = queue_ ? queue_->owner.lock() : nullptr;
            if (!state || __stop_requested(*rcvr_)) {
                std::execution::set_stopped(std::move(*rcvr_));
                return;
            }

            if (!state->try_accept()) {
                state->record_trace(__make_trace_event(
                    queue_,
                    session_,
                    trace_event_kind::stopped));
                std::execution::set_stopped(std::move(*rcvr_));
                return;
            }

            std::optional<worker_generation> accepted_generation;
            try {
                if (session_) {
                    accepted_generation = __admit_session_for_command(
                        session_,
                        "forge::accel::mock command: session is not usable");
                } else {
                    accepted_generation = __admit_queue_for_command(
                        queue_,
                        "forge::accel::mock command: queue device is not usable");
                }
            } catch (const __stopped_signal&) {
                state->record_trace(__make_trace_event(
                    queue_,
                    session_,
                    trace_event_kind::stopped));
                state->finish_one();
                std::execution::set_stopped(std::move(*rcvr_));
                return;
            } catch (...) {
                auto ep = std::current_exception();
                __record_trace_exception(
                    state,
                    queue_,
                    session_,
                    std::nullopt,
                    command_id{},
                    ep);
                state->finish_one();
                std::execution::set_error(
                    std::move(*rcvr_),
                    std::move(ep));
                return;
            }

            state->record_trace(__make_trace_event(
                queue_,
                session_,
                trace_event_kind::submitted,
                accepted_generation));
            try {
                // The mock queue is backed by strand::scheduler, whose schedule()
                // path enqueues work instead of completing synchronously.
                auto sender = std::execution::schedule(queue_->scheduler());
                auto* op = op_.emplace_from([&]() -> op_t {
                    return std::execution::connect(
                        std::move(sender),
                        receiver_t{
                            state,
                            queue_,
                            session_,
                            accepted_generation,
                            std::move(*rcvr_),
                            std::move(*action_)});
                });
                std::execution::start(*op);
            } catch (...) {
                auto ep = std::current_exception();
                __record_trace_exception(
                    state,
                    queue_,
                    session_,
                    accepted_generation,
                    command_id{},
                    ep);
                state->finish_one();
                std::execution::set_error(
                    std::move(*rcvr_),
                    std::move(ep));
            }
        }

        std::shared_ptr<__queue_state> queue_;
        std::shared_ptr<__session_state> session_;
        std::optional<Action> action_;
        std::optional<R> rcvr_;
        __op_box<op_t> op_;
    };

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{
            std::move(queue),
            std::move(session),
            std::move(action),
            std::move(rcvr)};
    }

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
              && std::copy_constructible<Action>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{queue, session, Action(action), std::move(rcvr)};
    }
};

struct __event_query_sender {
    using sender_concept = std::execution::sender_t;
    using completion_signatures = std::execution::completion_signatures<
        std::execution::set_value_t(event_snapshot),
        std::execution::set_error_t(std::exception_ptr),
        std::execution::set_stopped_t()>;

    std::shared_ptr<__event_state> event;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    struct __op {
        using operation_state_concept = std::execution::operation_state_t;

        std::shared_ptr<__event_state> event;
        std::optional<R> rcvr;

        void start() & noexcept {
            if (!event) {
                std::execution::set_error(
                    std::move(*rcvr),
                    std::make_exception_ptr(operation_error{
                        error_kind::invalid_event,
                        "forge::accel::mock::query_event: invalid event"}));
                return;
            }
            std::execution::set_value(std::move(*rcvr), event->snapshot());
        }
    };

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{std::move(event), std::move(rcvr)};
    }

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{event, std::move(rcvr)};
    }
};

template<class R>
struct __event_record_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<__state> state;
    std::shared_ptr<__event_state> event;
    event_generation target;
    R rcvr;

    void set_value() && noexcept {
        event->mark_completed(target);
        __current_state_guard guard{state.get()};
        state->finish_one();
        std::execution::set_value(std::move(rcvr));
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        __current_state_guard guard{state.get()};
        state->finish_one();
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
            std::execution::set_error(std::move(rcvr), static_cast<E&&>(e));
        } else {
            std::execution::set_error(
                std::move(rcvr),
                std::make_exception_ptr(static_cast<E&&>(e)));
        }
    }

    void set_stopped() && noexcept {
        __current_state_guard guard{state.get()};
        state->finish_one();
        std::execution::set_stopped(std::move(rcvr));
    }

    auto get_env() const noexcept(noexcept(std::execution::get_env(rcvr)))
        -> decltype(std::execution::get_env(rcvr)) {
        return std::execution::get_env(rcvr);
    }
};

struct __event_record_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__queue_state> queue;
    std::shared_ptr<__event_state> event;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> __void_completion_signatures {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
    struct __op {
        using operation_state_concept = std::execution::operation_state_t;
        using scheduler_t = strand::scheduler;
        using schedule_sender_t = decltype(std::execution::schedule(
            std::declval<scheduler_t>()));
        using receiver_t = __event_record_receiver<R>;
        using op_t = std::execution::connect_result_t<schedule_sender_t, receiver_t>;

        std::shared_ptr<__queue_state> queue;
        std::shared_ptr<__event_state> event;
        std::optional<R> rcvr;
        __op_box<op_t> op;

        void start() & noexcept {
            auto state = queue ? queue->owner.lock() : nullptr;
            if (!state || __stop_requested(*rcvr) || !state->try_accept()) {
                std::execution::set_stopped(std::move(*rcvr));
                return;
            }
            if (!event) {
                state->finish_one();
                std::execution::set_error(
                    std::move(*rcvr),
                    std::make_exception_ptr(operation_error{
                        error_kind::invalid_event,
                        "forge::accel::mock::record_event: invalid event"}));
                return;
            }

            auto target = event->reserve_record_generation();
            try {
                auto sender = std::execution::schedule(queue->scheduler());
                auto* connected = op.emplace_from([&]() -> op_t {
                    return std::execution::connect(
                        std::move(sender),
                        receiver_t{
                            state,
                            event,
                            target,
                            std::move(*rcvr)});
                });
                std::execution::start(*connected);
            } catch (...) {
                state->finish_one();
                std::execution::set_error(
                    std::move(*rcvr),
                    std::current_exception());
            }
        }
    };

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{std::move(queue), std::move(event), std::move(rcvr)};
    }

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{queue, event, std::move(rcvr)};
    }
};

template<class R>
struct __event_wait_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<__state> state;
    std::shared_ptr<__event_state> event;
    event_generation target;
    std::optional<std::chrono::steady_clock::time_point> deadline;
    R rcvr;

    void set_value() && noexcept {
        auto status = event->wait_until_generation_or_stopped(*state, target, deadline);
        __current_state_guard guard{state.get()};
        state->finish_one();
        if (status == command_status::stopped) {
            std::execution::set_stopped(std::move(rcvr));
            return;
        }
        if (status == command_status::timed_out) {
            std::execution::set_error(
                std::move(rcvr),
                std::make_exception_ptr(operation_error{
                    error_kind::timeout,
                    command_status::timed_out,
                    "forge::accel::mock::wait_event timed out"}));
            return;
        }
        std::execution::set_value(std::move(rcvr));
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        __current_state_guard guard{state.get()};
        state->finish_one();
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
            std::execution::set_error(std::move(rcvr), static_cast<E&&>(e));
        } else {
            std::execution::set_error(
                std::move(rcvr),
                std::make_exception_ptr(static_cast<E&&>(e)));
        }
    }

    void set_stopped() && noexcept {
        __current_state_guard guard{state.get()};
        state->finish_one();
        std::execution::set_stopped(std::move(rcvr));
    }

    auto get_env() const noexcept(noexcept(std::execution::get_env(rcvr)))
        -> decltype(std::execution::get_env(rcvr)) {
        return std::execution::get_env(rcvr);
    }
};

struct __event_wait_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__queue_state> queue;
    std::shared_ptr<__event_state> event;
    event_wait_options options;
    bool synchronize_current = false;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> __void_completion_signatures {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
    struct __op {
        using operation_state_concept = std::execution::operation_state_t;
        using scheduler_t = strand::scheduler;
        using schedule_sender_t = decltype(std::execution::schedule(
            std::declval<scheduler_t>()));
        using receiver_t = __event_wait_receiver<R>;
        using op_t = std::execution::connect_result_t<schedule_sender_t, receiver_t>;

        std::shared_ptr<__queue_state> queue;
        std::shared_ptr<__event_state> event;
        event_wait_options options;
        bool synchronize_current = false;
        std::optional<R> rcvr;
        __op_box<op_t> op;

        void start() & noexcept {
            auto state = queue ? queue->owner.lock() : nullptr;
            if (!state || __stop_requested(*rcvr) || !state->try_accept()) {
                std::execution::set_stopped(std::move(*rcvr));
                return;
            }
            if (!event) {
                state->finish_one();
                std::execution::set_error(
                    std::move(*rcvr),
                    std::make_exception_ptr(operation_error{
                        error_kind::invalid_event,
                        "forge::accel::mock::wait_event: invalid event"}));
                return;
            }

            auto target = synchronize_current
                ? event->recorded()
                : event->wait_target_generation();
            std::optional<std::chrono::steady_clock::time_point> deadline;
            if (options.timeout) {
                deadline = std::chrono::steady_clock::now() + *options.timeout;
            }
            try {
                auto sender = std::execution::schedule(queue->scheduler());
                auto* connected = op.emplace_from([&]() -> op_t {
                    return std::execution::connect(
                        std::move(sender),
                        receiver_t{
                            state,
                            event,
                            target,
                            deadline,
                            std::move(*rcvr)});
                });
                std::execution::start(*connected);
            } catch (...) {
                state->finish_one();
                std::execution::set_error(
                    std::move(*rcvr),
                    std::current_exception());
            }
        }
    };

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{
            std::move(queue),
            std::move(event),
            options,
            synchronize_current,
            std::move(rcvr)};
    }

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{
            queue,
            event,
            options,
            synchronize_current,
            std::move(rcvr)};
    }
};

template<class R, class Packet, class Handler>
struct __packet_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<__state> state;
    std::shared_ptr<__queue_state> queue;
    std::shared_ptr<__session_state> session;
    std::shared_ptr<Packet> packet;
    Handler handler;
    worker_generation accepted_generation;
    std::optional<std::chrono::steady_clock::time_point> deadline;
    R rcvr;

    void set_value() && noexcept {
        __current_state_guard guard{state.get()};
        const auto command = packet ? packet->id : command_id{};
        try {
            state->record_trace(__make_trace_event(
                queue,
                session,
                trace_event_kind::started,
                accepted_generation,
                command));
            __validate_session_for_execution(
                session,
                accepted_generation,
                "forge::accel::mock packet command: session is not usable");
            if (deadline && std::chrono::steady_clock::now() >= *deadline) {
                packet->status = command_status::timed_out;
                throw operation_error{
                    error_kind::timeout,
                    command_status::timed_out,
                    "forge::accel::mock packet command timed out"};
            }
            __invoke_packet_handler(*packet, handler);
            state->record_trace(__make_trace_event(
                queue,
                session,
                trace_event_kind::completed,
                accepted_generation,
                command));
            state->finish_one();
            std::execution::set_value(std::move(rcvr), std::move(*packet));
        } catch (const __stopped_signal&) {
            state->record_trace(__make_trace_event(
                queue,
                session,
                trace_event_kind::stopped,
                accepted_generation,
                command));
            state->finish_one();
            std::execution::set_stopped(std::move(rcvr));
        } catch (...) {
            auto ep = std::current_exception();
            __record_trace_exception(
                state,
                queue,
                session,
                accepted_generation,
                command,
                ep);
            state->finish_one();
            std::execution::set_error(std::move(rcvr), std::move(ep));
        }
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        __current_state_guard guard{state.get()};
        const auto command = packet ? packet->id : command_id{};
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
            __record_trace_exception(
                state,
                queue,
                session,
                accepted_generation,
                command,
                e);
        } else {
            auto event = __make_trace_event(
                queue,
                session,
                trace_event_kind::error,
                accepted_generation,
                command);
            event.error = error_kind::unknown;
            event.status = command_status::failed;
            state->record_trace(event);
        }
        state->finish_one();
        if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
            std::execution::set_error(std::move(rcvr), static_cast<E&&>(e));
        } else {
            std::execution::set_error(
                std::move(rcvr),
                std::make_exception_ptr(static_cast<E&&>(e)));
        }
    }

    void set_stopped() && noexcept {
        __current_state_guard guard{state.get()};
        state->record_trace(__make_trace_event(
            queue,
            session,
            trace_event_kind::stopped,
            accepted_generation,
            packet ? packet->id : command_id{}));
        state->finish_one();
        std::execution::set_stopped(std::move(rcvr));
    }

    auto get_env() const noexcept(noexcept(std::execution::get_env(rcvr)))
        -> decltype(std::execution::get_env(rcvr)) {
        return std::execution::get_env(rcvr);
    }
};

template<class Packet, class Handler>
struct __packet_sender {
    using sender_concept = std::execution::sender_t;
    using completion_signatures = std::execution::completion_signatures<
        std::execution::set_value_t(Packet),
        std::execution::set_error_t(std::exception_ptr),
        std::execution::set_stopped_t()>;

    std::shared_ptr<__queue_state> queue;
    std::shared_ptr<__session_state> session;
    std::shared_ptr<Packet> packet;
    Handler handler;
    command_options options;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    struct __op {
        using operation_state_concept = std::execution::operation_state_t;
        using scheduler_t = strand::scheduler;
        using schedule_sender_t = decltype(std::execution::schedule(
            std::declval<scheduler_t>()));
        using receiver_t = __packet_receiver<R, Packet, Handler>;
        using op_t = std::execution::connect_result_t<schedule_sender_t, receiver_t>;

        __op(
            std::shared_ptr<__queue_state> queue,
            std::shared_ptr<__session_state> session,
            std::shared_ptr<Packet> packet,
            Handler handler,
            command_options options,
            R rcvr)
            : queue_(std::move(queue))
            , session_(std::move(session))
            , packet_(std::move(packet))
            , handler_(std::move(handler))
            , options_(options)
            , rcvr_(std::move(rcvr))
        {}

        __op(__op&&) = delete;
        __op& operator=(__op&&) = delete;
        __op(const __op&) = delete;
        __op& operator=(const __op&) = delete;

        void start() & noexcept {
            auto state = queue_ ? queue_->owner.lock() : nullptr;
            if (!state || __stop_requested(*rcvr_)) {
                std::execution::set_stopped(std::move(*rcvr_));
                return;
            }
            const auto command = packet_ ? packet_->id : command_id{};

            if (!state->try_accept()) {
                state->record_trace(__make_trace_event(
                    queue_,
                    session_,
                    trace_event_kind::stopped,
                    std::nullopt,
                    command));
                std::execution::set_stopped(std::move(*rcvr_));
                return;
            }

            worker_generation accepted_generation{};
            try {
                accepted_generation = __admit_session_for_command(
                    session_,
                    "forge::accel::mock packet command: session is not usable");
            } catch (const __stopped_signal&) {
                state->record_trace(__make_trace_event(
                    queue_,
                    session_,
                    trace_event_kind::stopped,
                    std::nullopt,
                    command));
                state->finish_one();
                std::execution::set_stopped(std::move(*rcvr_));
                return;
            } catch (...) {
                auto ep = std::current_exception();
                __record_trace_exception(
                    state,
                    queue_,
                    session_,
                    std::nullopt,
                    command,
                    ep);
                state->finish_one();
                std::execution::set_error(
                    std::move(*rcvr_),
                    std::move(ep));
                return;
            }

            std::optional<std::chrono::steady_clock::time_point> deadline;
            if (options_.timeout) {
                deadline = std::chrono::steady_clock::now() + *options_.timeout;
            }

            state->record_trace(__make_trace_event(
                queue_,
                session_,
                trace_event_kind::submitted,
                accepted_generation,
                command));
            try {
                auto sender = std::execution::schedule(queue_->scheduler());
                auto* op = op_.emplace_from([&]() -> op_t {
                    return std::execution::connect(
                        std::move(sender),
                        receiver_t{
                            state,
                            queue_,
                            session_,
                            packet_,
                            std::move(*handler_),
                            accepted_generation,
                            deadline,
                            std::move(*rcvr_)});
                });
                std::execution::start(*op);
            } catch (...) {
                auto ep = std::current_exception();
                __record_trace_exception(
                    state,
                    queue_,
                    session_,
                    accepted_generation,
                    command,
                    ep);
                state->finish_one();
                std::execution::set_error(
                    std::move(*rcvr_),
                    std::move(ep));
            }
        }

        std::shared_ptr<__queue_state> queue_;
        std::shared_ptr<__session_state> session_;
        std::shared_ptr<Packet> packet_;
        std::optional<Handler> handler_;
        command_options options_;
        std::optional<R> rcvr_;
        __op_box<op_t> op_;
    };

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{
            std::move(queue),
            std::move(session),
            std::move(packet),
            std::move(handler),
            options,
            std::move(rcvr)};
    }
};

template<class Action>
auto __make_command_sender(std::shared_ptr<__queue_state> queue, Action&& action)
    -> __command_sender<std::decay_t<Action>> {
    return __command_sender<std::decay_t<Action>>{
        std::move(queue),
        nullptr,
        static_cast<Action&&>(action)};
}

template<class Action>
auto __make_session_command_sender(
    std::shared_ptr<__queue_state> queue,
    std::shared_ptr<__session_state> session,
    Action&& action) -> __command_sender<std::decay_t<Action>> {
    return __command_sender<std::decay_t<Action>>{
        std::move(queue),
        std::move(session),
        static_cast<Action&&>(action)};
}

} // namespace forge::accel::mock::__detail
