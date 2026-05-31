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

#include "error.hpp"
#include "../resource_context.hpp"
#include "../resource_policy.hpp"
#include "../strand.hpp"

#include <execution>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace forge::accel {

struct context_options {
    std::size_t thread_count = 1;
    std::optional<std::size_t> queue_capacity = std::nullopt;
    std::pmr::memory_resource* memory = forge::default_memory_resource();
};

class context;
class queue;
class device;
class device_session;
class event;
template<class T>
class host_buffer;
template<class T>
class device_buffer;

namespace __detail {

using __void_completion_signatures = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;

struct __state;
inline thread_local __state* __current_state = nullptr;

struct __stopped_signal {};

struct __session_state {
    std::atomic<bool> reset_requested{false};
};

struct __event_state {
    void mark_ready() noexcept {
        {
            std::lock_guard lk{mtx};
            ready = true;
        }
        cv.notify_all();
    }

    [[nodiscard]] bool is_ready() const noexcept {
        std::lock_guard lk{mtx};
        return ready;
    }

    [[nodiscard]] bool wait_until_ready_or_stopped(const __state& state) noexcept;

    mutable std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;
};

struct __state : std::enable_shared_from_this<__state> {
    explicit __state(context_options options)
        : memory(normalize_memory_resource(options.memory))
        , runtime(resource_context_options{
              .thread_count = options.thread_count == 0 ? 1 : options.thread_count,
              .queue_capacity = std::nullopt,
              .memory = memory,
          })
        , serial(runtime.get_scheduler(), strand_options{.memory = memory})
        , queue_capacity(options.queue_capacity)
    {}

    ~__state() noexcept {
        shutdown();
        wait();
    }

    __state(const __state&) = delete;
    __state& operator=(const __state&) = delete;

    bool try_accept() noexcept {
        std::lock_guard lk{mtx};
        if (closed || stop_requested) {
            return false;
        }
        if (queue_capacity && pending >= *queue_capacity) {
            return false;
        }
        ++pending;
        return true;
    }

    void finish_one() noexcept {
        std::lock_guard lk{mtx};
        if (pending > 0) {
            --pending;
        }
        if (pending == 0) {
            cv.notify_all();
        }
    }

    void close() noexcept {
        std::lock_guard lk{mtx};
        closed = true;
        if (pending == 0) {
            cv.notify_all();
        }
    }

    void request_stop() noexcept {
        {
            std::lock_guard lk{mtx};
            stop_requested = true;
        }
        serial.shutdown();
        runtime.request_stop();
        cv.notify_all();
    }

    void shutdown() noexcept {
        close();
        request_stop();
    }

    void wait() noexcept {
        if (__current_state == this) {
            return;
        }
        serial.wait();
        runtime.wait();
        std::unique_lock lk{mtx};
        cv.wait(lk, [this] { return pending == 0; });
    }

    [[nodiscard]] auto scheduler() noexcept -> strand::scheduler {
        return serial.get_scheduler();
    }

    [[nodiscard]] bool is_closed() const noexcept {
        std::lock_guard lk{mtx};
        return closed || stop_requested;
    }

    [[nodiscard]] bool stop_requested_now() const noexcept {
        std::lock_guard lk{mtx};
        return stop_requested;
    }

    [[nodiscard]] auto memory_resource() const noexcept -> std::pmr::memory_resource* {
        return memory;
    }

    std::pmr::memory_resource* memory;
    resource_context runtime;
    strand serial;
    std::optional<std::size_t> queue_capacity;
    mutable std::mutex mtx;
    std::condition_variable cv;
    std::size_t pending = 0;
    bool closed = false;
    bool stop_requested = false;
};

inline bool __event_state::wait_until_ready_or_stopped(const __state& state) noexcept {
    std::unique_lock lk{mtx};
    while (!ready) {
        if (state.stop_requested_now()) {
            return false;
        }
        cv.wait_for(lk, std::chrono::milliseconds{1});
    }
    return true;
}

struct __current_state_guard {
    explicit __current_state_guard(__state* state) noexcept
        : previous(__current_state) {
        __current_state = state;
    }

    ~__current_state_guard() {
        __current_state = previous;
    }

    __state* previous;
};

template<class R>
bool __stop_requested(const R& rcvr) noexcept {
    if constexpr (requires {
                      std::execution::get_stop_token(
                          std::execution::get_env(rcvr));
                  }) {
        return std::execution::get_stop_token(
            std::execution::get_env(rcvr)).stop_requested();
    } else {
        return false;
    }
}

template<class R, class Action>
struct __command_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<__state> state;
    R rcvr;
    Action action;

    void set_value() && noexcept {
        __current_state_guard guard{state.get()};
        try {
            std::invoke(std::move(action));
            state->finish_one();
            std::execution::set_value(std::move(rcvr));
        } catch (const __stopped_signal&) {
            state->finish_one();
            std::execution::set_stopped(std::move(rcvr));
        } catch (...) {
            state->finish_one();
            std::execution::set_error(std::move(rcvr), std::current_exception());
        }
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

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
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

    std::shared_ptr<__state> state;
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

        __op(std::shared_ptr<__state> state, Action action, R rcvr)
            : state_(std::move(state))
            , action_(std::move(action))
            , rcvr_(std::move(rcvr))
        {}

        __op(__op&&) = delete;
        __op& operator=(__op&&) = delete;
        __op(const __op&) = delete;
        __op& operator=(const __op&) = delete;

        void start() & noexcept {
            if (!state_ || __stop_requested(*rcvr_) || !state_->try_accept()) {
                std::execution::set_stopped(std::move(*rcvr_));
                return;
            }

            try {
                auto sender = std::execution::schedule(state_->scheduler());
                auto* op = op_.emplace_from([&]() -> op_t {
                    return std::execution::connect(
                        std::move(sender),
                        receiver_t{
                            state_,
                            std::move(*rcvr_),
                            std::move(*action_)});
                });
                std::execution::start(*op);
            } catch (...) {
                state_->finish_one();
                std::execution::set_error(
                    std::move(*rcvr_),
                    std::current_exception());
            }
        }

        std::shared_ptr<__state> state_;
        std::optional<Action> action_;
        std::optional<R> rcvr_;
        __op_box<op_t> op_;
    };

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{std::move(state), std::move(action), std::move(rcvr)};
    }

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
              && std::copy_constructible<Action>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{state, Action(action), std::move(rcvr)};
    }
};

template<class Action>
auto __make_command_sender(std::shared_ptr<__state> state, Action&& action)
    -> __command_sender<std::decay_t<Action>> {
    return __command_sender<std::decay_t<Action>>{
        std::move(state),
        static_cast<Action&&>(action)};
}

} // namespace __detail

class event {
public:
    event()
        : state_(std::make_shared<__detail::__event_state>())
    {}

    [[nodiscard]] bool ready() const noexcept {
        return state_ && state_->is_ready();
    }

private:
    friend auto record_event(queue&, event);
    friend auto wait_event(queue&, event);

    std::shared_ptr<__detail::__event_state> state_;
};

class queue {
public:
    queue() = default;

    [[nodiscard]] bool closed() const noexcept {
        auto state = state_.lock();
        return !state || state->is_closed();
    }

private:
    explicit queue(std::shared_ptr<__detail::__state> state)
        : state_(state) {}

    friend class context;
    friend class device;
    friend class device_session;
    template<class Action>
    friend auto __detail::__make_command_sender(
        std::shared_ptr<__detail::__state>,
        Action&&) -> __detail::__command_sender<std::decay_t<Action>>;
    template<class T>
    friend auto copy_to_device(queue&, device_buffer<T>&, std::span<const T>);
    template<class T>
    friend auto copy_to_host(queue&, std::span<T>, const device_buffer<T>&);
    template<class T>
    friend auto copy_device_to_device(queue&, device_buffer<T>&, const device_buffer<T>&);
    template<class F>
    friend auto submit(queue&, F&&);
    friend auto record_event(queue&, event);
    friend auto wait_event(queue&, event);
    friend auto fence(queue&);

    std::weak_ptr<__detail::__state> state_;
};

class context {
public:
    explicit context(context_options options = {})
        : state_(std::allocate_shared<__detail::__state>(
              std::pmr::polymorphic_allocator<__detail::__state>{
                  normalize_memory_resource(options.memory)},
              options)) {}

    ~context() noexcept {
        shutdown();
        wait();
    }

    context(const context&) = delete;
    context& operator=(const context&) = delete;
    context(context&&) = delete;
    context& operator=(context&&) = delete;

    [[nodiscard]] queue get_queue() noexcept {
        return queue{state_};
    }

    [[nodiscard]] device get_device() noexcept;

    void close() noexcept {
        state_->close();
    }

    void request_stop() noexcept {
        state_->request_stop();
    }

    void shutdown() noexcept {
        state_->shutdown();
    }

    void wait() noexcept {
        state_->wait();
    }

private:
    friend class device;
    friend class device_session;
    template<class T>
    friend class host_buffer;
    template<class T>
    friend class device_buffer;

    std::shared_ptr<__detail::__state> state_;
};

class device_session {
public:
    device_session() = default;

    [[nodiscard]] queue& get_queue() noexcept {
        return queue_;
    }

    [[nodiscard]] const queue& get_queue() const noexcept {
        return queue_;
    }

    void reset() noexcept {
        if (session_) {
            session_->reset_requested.store(true, std::memory_order_release);
        }
    }

    [[nodiscard]] bool reset_requested() const noexcept {
        return !session_ ||
            session_->reset_requested.load(std::memory_order_acquire);
    }

private:
    explicit device_session(std::shared_ptr<__detail::__state> state)
        : queue_(state)
        , session_(state
              ? std::allocate_shared<__detail::__session_state>(
                    std::pmr::polymorphic_allocator<__detail::__session_state>{
                        state->memory_resource()})
              : nullptr) {}

    template<class F>
    friend auto submit(device_session&, F&&);
    template<class Request, class Response, class Handler>
    friend auto submit_message(device_session&, Request, Response&, Handler&&);
    friend class device;

    queue queue_;
    std::shared_ptr<__detail::__session_state> session_;
};

class device {
public:
    device() = default;

    [[nodiscard]] queue get_queue() const noexcept {
        return queue{state_.lock()};
    }

    [[nodiscard]] device_session open_session() const {
        return device_session{state_.lock()};
    }

    [[nodiscard]] bool available() const noexcept {
        auto state = state_.lock();
        return state && !state->is_closed();
    }

private:
    explicit device(std::shared_ptr<__detail::__state> state)
        : state_(state) {}

    friend class context;

    std::weak_ptr<__detail::__state> state_;
};

inline auto context::get_device() noexcept -> device {
    return device{state_};
}

template<class T>
class host_buffer {
    static_assert(std::is_trivially_copyable_v<T>,
                  "forge::accel::host_buffer<T> v1 requires trivially copyable T");

public:
    using value_type = T;

    host_buffer(context& ctx, std::size_t size)
        : data_(std::pmr::polymorphic_allocator<T>{
              ctx.state_->memory_resource()})
    {
        data_.resize(size);
    }

    host_buffer(const host_buffer&) = delete;
    host_buffer& operator=(const host_buffer&) = delete;
    host_buffer(host_buffer&&) noexcept = default;
    host_buffer& operator=(host_buffer&&) noexcept = default;
    ~host_buffer() = default;

    [[nodiscard]] std::size_t size() const noexcept {
        return data_.size();
    }

    [[nodiscard]] auto span() noexcept -> std::span<T> {
        return std::span<T>{data_.data(), data_.size()};
    }

    [[nodiscard]] auto span() const noexcept -> std::span<const T> {
        return std::span<const T>{data_.data(), data_.size()};
    }

private:
    std::pmr::vector<T> data_;
};

template<class T>
class device_buffer {
    static_assert(std::is_trivially_copyable_v<T>,
                  "forge::accel::device_buffer<T> v1 requires trivially copyable T");

public:
    using value_type = T;

    device_buffer(context& ctx, std::size_t size)
        : data_(std::pmr::polymorphic_allocator<T>{
              ctx.state_->memory_resource()})
    {
        data_.resize(size);
    }

    device_buffer(const device_buffer&) = delete;
    device_buffer& operator=(const device_buffer&) = delete;
    device_buffer(device_buffer&&) noexcept = default;
    device_buffer& operator=(device_buffer&&) noexcept = default;
    ~device_buffer() = default;

    [[nodiscard]] std::size_t size() const noexcept {
        return data_.size();
    }

    [[nodiscard]] auto span() noexcept -> std::span<T> {
        return std::span<T>{data_.data(), data_.size()};
    }

    [[nodiscard]] auto span() const noexcept -> std::span<const T> {
        return std::span<const T>{data_.data(), data_.size()};
    }

private:
    template<class U>
    friend auto copy_to_device(queue&, device_buffer<U>&, std::span<const U>);
    template<class U>
    friend auto copy_to_host(queue&, std::span<U>, const device_buffer<U>&);
    template<class U>
    friend auto copy_device_to_device(queue&, device_buffer<U>&, const device_buffer<U>&);

    std::pmr::vector<T> data_;
};

template<class T>
auto copy_to_device(queue& q, device_buffer<T>& dst, std::span<const T> src) {
    return __detail::__make_command_sender(
        q.state_.lock(),
        [dst = &dst, src] {
            if (!dst) {
                throw operation_error{
                    error_kind::invalid_buffer,
                    "forge::accel::copy_to_device: null destination"};
            }
            if (dst->data_.size() != src.size()) {
                throw operation_error{
                    error_kind::size_mismatch,
                    "forge::accel::copy_to_device: size mismatch"};
            }
            std::copy(src.begin(), src.end(), dst->data_.begin());
        });
}

template<class T>
auto copy_to_device(queue& q, device_buffer<T>& dst, std::span<T> src) {
    return copy_to_device(q, dst, std::span<const T>{src});
}

template<class T>
auto copy_to_device_typed(queue& q, device_buffer<T>& dst, std::span<const T> src) {
    return __typed_detail::void_sender(copy_to_device(q, dst, src));
}

template<class T>
auto copy_to_device_typed(queue& q, device_buffer<T>& dst, std::span<T> src) {
    return copy_to_device_typed(q, dst, std::span<const T>{src});
}

template<class T>
auto copy_to_host(queue& q, std::span<T> dst, const device_buffer<T>& src) {
    return __detail::__make_command_sender(
        q.state_.lock(),
        [dst, src = &src] {
            if (!src) {
                throw operation_error{
                    error_kind::invalid_buffer,
                    "forge::accel::copy_to_host: null source"};
            }
            if (dst.size() != src->data_.size()) {
                throw operation_error{
                    error_kind::size_mismatch,
                    "forge::accel::copy_to_host: size mismatch"};
            }
            std::copy(src->data_.begin(), src->data_.end(), dst.begin());
        });
}

template<class T>
auto copy_to_host_typed(queue& q, std::span<T> dst, const device_buffer<T>& src) {
    return __typed_detail::void_sender(copy_to_host(q, dst, src));
}

template<class T>
auto copy_device_to_device(queue& q, device_buffer<T>& dst, const device_buffer<T>& src) {
    return __detail::__make_command_sender(
        q.state_.lock(),
        [dst = &dst, src = &src] {
            if (!dst || !src) {
                throw operation_error{
                    error_kind::invalid_buffer,
                    "forge::accel::copy_device_to_device: null buffer"};
            }
            if (dst->data_.size() != src->data_.size()) {
                throw operation_error{
                    error_kind::size_mismatch,
                    "forge::accel::copy_device_to_device: size mismatch"};
            }
            std::copy(src->data_.begin(), src->data_.end(), dst->data_.begin());
        });
}

template<class T>
auto copy_device_to_device_typed(
    queue& q,
    device_buffer<T>& dst,
    const device_buffer<T>& src) {
    return __typed_detail::void_sender(copy_device_to_device(q, dst, src));
}

template<class F>
auto submit(queue& q, F&& command) {
    return __detail::__make_command_sender(
        q.state_.lock(),
        [command = std::forward<F>(command)]() mutable {
            std::invoke(command);
        });
}

template<class F>
auto submit_typed(queue& q, F&& command) {
    return __typed_detail::void_sender(
        submit(q, static_cast<F&&>(command)));
}

template<class F>
auto submit(device_session& session, F&& command) {
    auto session_state = session.session_;
    return submit(
        session.get_queue(),
        [session_state, command = std::forward<F>(command)]() mutable {
            if (!session_state ||
                session_state->reset_requested.load(std::memory_order_acquire)) {
                throw __detail::__stopped_signal{};
            }
            std::invoke(command);
        });
}

template<class F>
auto submit_typed(device_session& session, F&& command) {
    return __typed_detail::void_sender(
        submit(session, static_cast<F&&>(command)));
}

template<class Request, class Response, class Handler>
auto submit_message(
    device_session& session,
    Request request,
    Response& response,
    Handler&& handler) {
    return submit(
        session,
        [request = std::move(request),
         response = &response,
         handler = std::forward<Handler>(handler)]() mutable {
            using result_t = std::invoke_result_t<Handler&, Request&, Response&>;
            if constexpr (std::is_same_v<result_t, command_status>) {
                const auto status = std::invoke(handler, request, *response);
                if (status == command_status::stopped) {
                    throw __detail::__stopped_signal{};
                }
                if (status != command_status::ok) {
                    throw command_error{status};
                }
            } else {
                std::invoke(handler, request, *response);
            }
        });
}

template<class Request, class Response, class Handler>
auto submit_message_typed(
    device_session& session,
    Request request,
    Response& response,
    Handler&& handler) {
    return __typed_detail::void_sender(
        submit_message(
            session,
            std::move(request),
            response,
            static_cast<Handler&&>(handler)));
}

inline auto record_event(queue& q, event ev) {
    return __detail::__make_command_sender(
        q.state_.lock(),
        [state = std::move(ev.state_)] {
            if (!state) {
                throw operation_error{
                    error_kind::invalid_event,
                    "forge::accel::record_event: invalid event"};
            }
            state->mark_ready();
        });
}

inline auto record_event_typed(queue& q, event ev) {
    return __typed_detail::void_sender(record_event(q, std::move(ev)));
}

inline auto wait_event(queue& q, event ev) {
    auto ctx = q.state_.lock();
    return __detail::__make_command_sender(
        ctx,
        [ctx, state = std::move(ev.state_)] {
            if (!ctx) {
                throw __detail::__stopped_signal{};
            }
            if (!state) {
                throw operation_error{
                    error_kind::invalid_event,
                    "forge::accel::wait_event: invalid event"};
            }
            if (!state->wait_until_ready_or_stopped(*ctx)) {
                throw __detail::__stopped_signal{};
            }
        });
}

inline auto wait_event_typed(queue& q, event ev) {
    return __typed_detail::void_sender(wait_event(q, std::move(ev)));
}

inline auto fence(queue& q) {
    return __detail::__make_command_sender(q.state_.lock(), [] {});
}

inline auto fence_typed(queue& q) {
    return __typed_detail::void_sender(fence(q));
}

} // namespace forge::accel
