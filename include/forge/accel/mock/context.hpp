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

#include "../error.hpp"
#include "../vocabulary.hpp"
#include "../../resource_context.hpp"
#include "../../resource_policy.hpp"
#include "../../strand.hpp"

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

namespace forge::accel::mock {

struct context_options {
    std::size_t thread_count = 1;
    std::optional<std::size_t> queue_capacity = std::nullopt;
    std::size_t device_count = 1;
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
template<class T>
auto flush(queue&, device_buffer<T>&);
template<class T>
auto invalidate(queue&, device_buffer<T>&);

struct command_options {
    std::optional<std::chrono::steady_clock::duration> timeout = std::nullopt;
};

struct event_wait_options {
    std::optional<std::chrono::steady_clock::duration> timeout = std::nullopt;
};

template<class Request, class Response>
struct command_packet {
    using request_type = Request;
    using response_type = Response;

    command_packet(command_id id, Request request, Response response)
        : id(id)
        , request(std::move(request))
        , response(std::move(response))
    {}

    command_id id{};
    Request request;
    Response response;
    command_status status = command_status::ok;
};

template<class Request, class Response>
command_packet(command_id, Request, Response) -> command_packet<Request, Response>;

struct event_snapshot {
    event_generation record_generation{};
    event_generation completed_generation{};
    bool ready = false;
};

namespace __detail {

using __void_completion_signatures = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;

struct __state;
inline thread_local __state* __current_state = nullptr;

struct __stopped_signal {};

struct __device_state;

struct __session_state {
    std::weak_ptr<__device_state> device;
    std::atomic<bool> reset_requested{false};
};

struct __queue_state;

struct __event_state {
    [[nodiscard]] auto reserve_record_generation() noexcept -> event_generation {
        std::lock_guard lk{mtx};
        return event_generation{++record_generation};
    }

    void mark_completed(event_generation generation) noexcept {
        {
            std::lock_guard lk{mtx};
            if (completed_generation < generation.value) {
                completed_generation = generation.value;
            }
        }
        cv.notify_all();
    }

    [[nodiscard]] auto snapshot() const noexcept -> event_snapshot {
        std::lock_guard lk{mtx};
        return event_snapshot{
            event_generation{record_generation},
            event_generation{completed_generation},
            record_generation != 0 && completed_generation >= record_generation};
    }

    [[nodiscard]] auto recorded() const noexcept -> event_generation {
        std::lock_guard lk{mtx};
        return event_generation{record_generation};
    }

    [[nodiscard]] auto completed() const noexcept -> event_generation {
        std::lock_guard lk{mtx};
        return event_generation{completed_generation};
    }

    [[nodiscard]] auto wait_target_generation() const noexcept -> event_generation {
        std::lock_guard lk{mtx};
        return event_generation{record_generation == 0 ? 1 : record_generation};
    }

    [[nodiscard]] command_status wait_until_generation_or_stopped(
        const __state& state,
        event_generation target,
        std::optional<std::chrono::steady_clock::time_point> deadline) noexcept;

    mutable std::mutex mtx;
    std::condition_variable cv;
    std::uint64_t record_generation = 0;
    std::uint64_t completed_generation = 0;
};

struct __state : std::enable_shared_from_this<__state> {
    explicit __state(context_options options)
        : memory(normalize_memory_resource(options.memory))
        , runtime(resource_context_options{
              .thread_count = options.thread_count == 0 ? 1 : options.thread_count,
              .queue_capacity = std::nullopt,
              .memory = memory,
          })
        , queue_capacity(options.queue_capacity)
        , device_count(options.device_count)
        , devices(std::pmr::polymorphic_allocator<std::shared_ptr<__device_state>>{memory})
        , queues(std::pmr::polymorphic_allocator<std::shared_ptr<__queue_state>>{memory})
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

    void request_stop() noexcept;

    void shutdown() noexcept {
        close();
        request_stop();
    }

    void wait() noexcept;

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

    void initialize_devices();
    [[nodiscard]] auto get_devices() const
        -> std::pmr::vector<std::shared_ptr<__device_state>>;
    [[nodiscard]] auto get_device(device_id id) const -> std::shared_ptr<__device_state>;
    [[nodiscard]] auto get_queue(queue_kind kind) -> std::shared_ptr<__queue_state>;
    [[nodiscard]] auto make_queue(
        queue_kind kind,
        std::shared_ptr<__device_state> device = nullptr) -> std::shared_ptr<__queue_state>;
    [[nodiscard]] auto snapshot_queues()
        -> std::pmr::vector<std::shared_ptr<__queue_state>>;

    std::pmr::memory_resource* memory;
    resource_context runtime;
    std::optional<std::size_t> queue_capacity;
    std::size_t device_count = 1;
    mutable std::mutex mtx;
    std::condition_variable cv;
    std::size_t pending = 0;
    bool closed = false;
    bool stop_requested = false;
    std::pmr::vector<std::shared_ptr<__device_state>> devices;
    std::pmr::vector<std::shared_ptr<__queue_state>> queues;
};

struct __device_state {
    __device_state(std::shared_ptr<__state> owner, device_info info)
        : owner(std::move(owner))
        , info(info)
    {}

    [[nodiscard]] bool available() const noexcept {
        auto state = owner.lock();
        return state && !state->is_closed() && info.available &&
            !lost.load(std::memory_order_acquire);
    }

    [[nodiscard]] auto current_info() const noexcept -> device_info {
        auto out = info;
        out.available = available();
        return out;
    }

    void mark_lost() noexcept {
        lost.store(true, std::memory_order_release);
    }

    void reset() noexcept {
        lost.store(false, std::memory_order_release);
    }

    std::weak_ptr<__state> owner;
    device_info info{};
    std::atomic<bool> lost{false};
};

struct __queue_state {
    __queue_state(
        std::shared_ptr<__state> owner_state,
        queue_kind queue_kind,
        std::shared_ptr<__device_state> bound_device = nullptr)
        : owner(owner_state)
        , device(std::move(bound_device))
        , serial(
              owner_state->runtime.get_scheduler(),
              strand_options{.memory = owner_state->memory})
        , kind(queue_kind)
    {}

    [[nodiscard]] auto scheduler() noexcept -> strand::scheduler {
        return serial.get_scheduler();
    }

    void shutdown() noexcept {
        serial.shutdown();
    }

    void wait() noexcept {
        serial.wait();
    }

    [[nodiscard]] bool device_available() const noexcept {
        return !device || device->available();
    }

    std::weak_ptr<__state> owner;
    std::shared_ptr<__device_state> device;
    strand serial;
    queue_kind kind = queue_kind::general;
};

inline void __state::request_stop() noexcept {
    {
        std::lock_guard lk{mtx};
        stop_requested = true;
    }
    for (auto& queue : snapshot_queues()) {
        queue->shutdown();
    }
    runtime.request_stop();
    cv.notify_all();
}

inline void __state::wait() noexcept {
    if (__current_state == this) {
        return;
    }
    for (auto& queue : snapshot_queues()) {
        queue->wait();
    }
    runtime.wait();
    std::unique_lock lk{mtx};
    cv.wait(lk, [this] { return pending == 0; });
}

inline void __state::initialize_devices() {
    auto self = shared_from_this();
    std::lock_guard lk{mtx};
    if (!devices.empty() || device_count == 0) {
        return;
    }
    for (std::size_t i = 0; i < device_count; ++i) {
        devices.push_back(std::allocate_shared<__device_state>(
            std::pmr::polymorphic_allocator<__device_state>{memory},
            self,
            device_info{
                .id = device_id{static_cast<std::uint32_t>(i)},
                .ordinal = static_cast<std::uint32_t>(i),
                .available = true,
            }));
    }
}

inline auto __state::get_devices() const
    -> std::pmr::vector<std::shared_ptr<__device_state>> {
    std::pmr::vector<std::shared_ptr<__device_state>> snapshot{
        std::pmr::polymorphic_allocator<std::shared_ptr<__device_state>>{memory}};
    std::lock_guard lk{mtx};
    snapshot = devices;
    return snapshot;
}

inline auto __state::get_device(device_id id) const -> std::shared_ptr<__device_state> {
    std::lock_guard lk{mtx};
    for (auto& device : devices) {
        if (device && device->info.id == id) {
            return device;
        }
    }
    return {};
}

inline auto __state::make_queue(
    queue_kind kind,
    std::shared_ptr<__device_state> device) -> std::shared_ptr<__queue_state> {
    auto self = shared_from_this();
    auto queue = std::allocate_shared<__queue_state>(
        std::pmr::polymorphic_allocator<__queue_state>{memory},
        std::move(self),
        kind,
        std::move(device));
    {
        std::lock_guard lk{mtx};
        queues.push_back(queue);
    }
    if (is_closed()) {
        queue->shutdown();
    }
    return queue;
}

inline auto __state::get_queue(queue_kind kind) -> std::shared_ptr<__queue_state> {
    auto self = shared_from_this();
    auto queue = std::allocate_shared<__queue_state>(
        std::pmr::polymorphic_allocator<__queue_state>{memory},
        std::move(self),
        kind,
        nullptr);
    bool should_shutdown = false;
    {
        std::lock_guard lk{mtx};
        for (auto& existing : queues) {
            if (existing && !existing->device && existing->kind == kind) {
                return existing;
            }
        }
        queues.push_back(queue);
        should_shutdown = closed || stop_requested;
    }
    if (should_shutdown) {
        queue->shutdown();
    }
    return queue;
}

inline auto __state::snapshot_queues()
    -> std::pmr::vector<std::shared_ptr<__queue_state>> {
    std::pmr::vector<std::shared_ptr<__queue_state>> snapshot{
        std::pmr::polymorphic_allocator<std::shared_ptr<__queue_state>>{memory}};
    std::lock_guard lk{mtx};
    snapshot = queues;
    return snapshot;
}

inline command_status __event_state::wait_until_generation_or_stopped(
    const __state& state,
    event_generation target,
    std::optional<std::chrono::steady_clock::time_point> deadline) noexcept {
    std::unique_lock lk{mtx};
    while (completed_generation < target.value) {
        if (state.stop_requested_now()) {
            return command_status::stopped;
        }
        if (deadline && std::chrono::steady_clock::now() >= *deadline) {
            return command_status::timed_out;
        }
        auto next_wake = std::chrono::steady_clock::now()
            + std::chrono::milliseconds{1};
        if (deadline && *deadline < next_wake) {
            next_wake = *deadline;
        }
        cv.wait_until(lk, next_wake);
    }
    return command_status::ok;
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

[[noreturn]] inline void __throw_for_command_status(command_status status) {
    if (status == command_status::stopped) {
        throw __stopped_signal{};
    }
    if (status == command_status::timed_out) {
        throw operation_error{
            error_kind::timeout,
            command_status::timed_out,
            "forge::accel::mock command timed out"};
    }
    if (status == command_status::aborted) {
        throw operation_error{
            error_kind::aborted,
            command_status::aborted,
            "forge::accel::mock command aborted"};
    }
    throw command_error{status};
}

template<class Packet, class Handler>
void __invoke_packet_handler(Packet& packet, Handler& handler) {
    using result_t = std::invoke_result_t<
        Handler&,
        typename Packet::request_type&,
        typename Packet::response_type&>;

    if constexpr (std::is_same_v<result_t, command_status>) {
        const auto status = std::invoke(handler, packet.request, packet.response);
        packet.status = status;
        if (status != command_status::ok) {
            __throw_for_command_status(status);
        }
    } else {
        std::invoke(handler, packet.request, packet.response);
        packet.status = command_status::ok;
    }
}

template<class R, class Action>
struct __command_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<__state> state;
    std::shared_ptr<__queue_state> queue;
    R rcvr;
    Action action;

    void set_value() && noexcept {
        __current_state_guard guard{state.get()};
        try {
            if (queue && !queue->device_available()) {
                throw operation_error{
                    error_kind::invalid_context,
                    "forge::accel::mock command: device is not available"};
            }
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

        __op(__op&&) = delete;
        __op& operator=(__op&&) = delete;
        __op(const __op&) = delete;
        __op& operator=(const __op&) = delete;

        void start() & noexcept {
            auto state = queue_ ? queue_->owner.lock() : nullptr;
            if (!state || __stop_requested(*rcvr_) || !state->try_accept()) {
                std::execution::set_stopped(std::move(*rcvr_));
                return;
            }

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
                            std::move(*rcvr_),
                            std::move(*action_)});
                });
                std::execution::start(*op);
            } catch (...) {
                state->finish_one();
                std::execution::set_error(
                    std::move(*rcvr_),
                    std::current_exception());
            }
        }

        std::shared_ptr<__queue_state> queue_;
        std::optional<Action> action_;
        std::optional<R> rcvr_;
        __op_box<op_t> op_;
    };

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{std::move(queue), std::move(action), std::move(rcvr)};
    }

    template<class R>
        requires std::execution::receiver_of<R, __void_completion_signatures>
              && std::copy_constructible<Action>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{queue, Action(action), std::move(rcvr)};
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
    std::optional<std::chrono::steady_clock::time_point> deadline;
    R rcvr;

    void set_value() && noexcept {
        __current_state_guard guard{state.get()};
        try {
            if (queue && !queue->device_available()) {
                throw operation_error{
                    error_kind::invalid_context,
                    "forge::accel::mock packet command: device is not available"};
            }
            if (!session ||
                session->reset_requested.load(std::memory_order_acquire)) {
                throw __stopped_signal{};
            }
            auto device = session->device.lock();
            if (!device || !device->available()) {
                throw operation_error{
                    error_kind::invalid_context,
                    "forge::accel::mock packet command: device is not available"};
            }
            if (deadline && std::chrono::steady_clock::now() >= *deadline) {
                packet->status = command_status::timed_out;
                throw operation_error{
                    error_kind::timeout,
                    command_status::timed_out,
                    "forge::accel::mock packet command timed out"};
            }
            __invoke_packet_handler(*packet, handler);
            state->finish_one();
            std::execution::set_value(std::move(rcvr), std::move(*packet));
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
            if (!state || __stop_requested(*rcvr_) || !state->try_accept()) {
                std::execution::set_stopped(std::move(*rcvr_));
                return;
            }

            std::optional<std::chrono::steady_clock::time_point> deadline;
            if (options_.timeout) {
                deadline = std::chrono::steady_clock::now() + *options_.timeout;
            }

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
                            deadline,
                            std::move(*rcvr_)});
                });
                std::execution::start(*op);
            } catch (...) {
                state->finish_one();
                std::execution::set_error(
                    std::move(*rcvr_),
                    std::current_exception());
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
        static_cast<Action&&>(action)};
}

} // namespace __detail

class event {
public:
    event()
        : state_(std::make_shared<__detail::__event_state>())
    {}

    [[nodiscard]] bool ready() const noexcept {
        return state_ && state_->snapshot().ready;
    }

    [[nodiscard]] auto record_generation() const noexcept -> event_generation {
        return state_ ? state_->recorded() : event_generation{};
    }

    [[nodiscard]] auto completed_generation() const noexcept -> event_generation {
        return state_ ? state_->completed() : event_generation{};
    }

    [[nodiscard]] auto query() const noexcept -> event_snapshot {
        return state_ ? state_->snapshot() : event_snapshot{};
    }

private:
    friend auto record_event(queue&, event);
    friend auto record_event_typed(queue&, event);
    friend auto wait_event(queue&, event);
    friend auto wait_event(queue&, event, event_wait_options);
    friend auto wait_event_typed(queue&, event);
    friend auto wait_event_typed(queue&, event, event_wait_options);
    friend auto query_event(event);
    friend auto query_event_typed(event);
    friend auto synchronize_event(queue&, event);
    friend auto synchronize_event(queue&, event, event_wait_options);
    friend auto synchronize_event_typed(queue&, event);
    friend auto synchronize_event_typed(queue&, event, event_wait_options);

    std::shared_ptr<__detail::__event_state> state_;
};

class queue {
public:
    queue() = default;

    [[nodiscard]] bool closed() const noexcept {
        auto queue = queue_.lock();
        auto state = queue ? queue->owner.lock() : nullptr;
        return !state || state->is_closed();
    }

    [[nodiscard]] auto kind() const noexcept -> queue_kind {
        auto queue = queue_.lock();
        return queue ? queue->kind : queue_kind::general;
    }

private:
    explicit queue(std::shared_ptr<__detail::__queue_state> queue)
        : queue_(std::move(queue)) {}

    friend class context;
    friend class device;
    friend class device_session;
    template<class Action>
    friend auto __detail::__make_command_sender(
        std::shared_ptr<__detail::__queue_state>,
        Action&&) -> __detail::__command_sender<std::decay_t<Action>>;
    template<class T>
    friend auto copy_to_device(queue&, device_buffer<T>&, std::span<const T>);
    template<class T>
    friend auto copy_to_host(queue&, std::span<T>, const device_buffer<T>&);
    template<class T>
    friend auto copy_device_to_device(queue&, device_buffer<T>&, const device_buffer<T>&);
    template<class T>
    friend auto flush(queue&, device_buffer<T>&);
    template<class T>
    friend auto invalidate(queue&, device_buffer<T>&);
    template<class F>
    friend auto submit(queue&, F&&);
    template<class Request, class Response, class Handler>
    friend auto submit_packet(
        device_session&,
        command_packet<Request, Response>,
        Handler&&,
        command_options);
    friend auto record_event(queue&, event);
    friend auto record_event_typed(queue&, event);
    friend auto wait_event(queue&, event);
    friend auto wait_event(queue&, event, event_wait_options);
    friend auto wait_event_typed(queue&, event);
    friend auto wait_event_typed(queue&, event, event_wait_options);
    friend auto synchronize_event(queue&, event);
    friend auto synchronize_event(queue&, event, event_wait_options);
    friend auto synchronize_event_typed(queue&, event);
    friend auto synchronize_event_typed(queue&, event, event_wait_options);
    friend auto fence(queue&);

    std::weak_ptr<__detail::__queue_state> queue_;
};

class context {
public:
    explicit context(context_options options = {})
        : state_(std::allocate_shared<__detail::__state>(
              std::pmr::polymorphic_allocator<__detail::__state>{
                  normalize_memory_resource(options.memory)},
              options)) {
        state_->initialize_devices();
    }

    ~context() noexcept {
        shutdown();
        wait();
    }

    context(const context&) = delete;
    context& operator=(const context&) = delete;
    context(context&&) = delete;
    context& operator=(context&&) = delete;

    [[nodiscard]] queue get_queue(queue_kind kind = queue_kind::general) {
        return queue{state_->get_queue(kind)};
    }

    [[nodiscard]] device get_device(device_id id = {}) noexcept;
    [[nodiscard]] auto devices() const -> std::vector<device>;
    [[nodiscard]] auto device_infos() const -> std::vector<device_info>;

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
    explicit device_session(
        std::shared_ptr<__detail::__state> state,
        std::shared_ptr<__detail::__device_state> device)
        : queue_(state
              ? state->make_queue(queue_kind::command, device)
              : nullptr)
        , session_(state && device && device->available()
              ? std::allocate_shared<__detail::__session_state>(
                    std::pmr::polymorphic_allocator<__detail::__session_state>{
                        state->memory_resource()})
              : nullptr) {
        if (session_) {
            session_->device = std::move(device);
        }
    }

    template<class F>
    friend auto submit(device_session&, F&&);
    template<class Request, class Response, class Handler>
    friend auto submit_message(device_session&, Request, Response&, Handler&&);
    template<class Request, class Response, class Handler>
    friend auto submit_packet(
        device_session&,
        command_packet<Request, Response>,
        Handler&&,
        command_options);
    friend class device;

    queue queue_;
    std::shared_ptr<__detail::__session_state> session_;
};

class device {
public:
    device() = default;

    [[nodiscard]] queue get_queue(queue_kind kind = queue_kind::general) const {
        auto device_state = device_.lock();
        auto state = device_state ? device_state->owner.lock() : nullptr;
        return queue{state ? state->make_queue(kind, device_state) : nullptr};
    }

    [[nodiscard]] device_session open_session() const {
        auto device_state = device_.lock();
        auto state = device_state ? device_state->owner.lock() : nullptr;
        return device_session{std::move(state), std::move(device_state)};
    }

    [[nodiscard]] bool available() const noexcept {
        auto device_state = device_.lock();
        return device_state && device_state->available();
    }

    [[nodiscard]] auto info() const noexcept -> device_info {
        auto device_state = device_.lock();
        if (device_state) {
            return device_state->current_info();
        }
        auto out = device_info{};
        out.available = false;
        return out;
    }

    void mark_lost() noexcept {
        if (auto device_state = device_.lock()) {
            device_state->mark_lost();
        }
    }

    void reset() noexcept {
        if (auto device_state = device_.lock()) {
            device_state->reset();
        }
    }

private:
    explicit device(std::shared_ptr<__detail::__device_state> device)
        : device_(std::move(device)) {}

    friend class context;

    std::weak_ptr<__detail::__device_state> device_;
};

inline auto context::get_device(device_id id) noexcept -> device {
    return device{state_->get_device(id)};
}

inline auto context::devices() const -> std::vector<device> {
    std::vector<device> out;
    for (auto& item : state_->get_devices()) {
        out.emplace_back(device{std::move(item)});
    }
    return out;
}

inline auto context::device_infos() const -> std::vector<device_info> {
    std::vector<device_info> out;
    for (auto& item : state_->get_devices()) {
        if (item) {
            out.push_back(item->current_info());
        }
    }
    return out;
}

template<class T>
class host_buffer {
    static_assert(std::is_trivially_copyable_v<T>,
                  "forge::accel::mock::host_buffer<T> requires trivially copyable T");

public:
    using value_type = T;

    host_buffer(
        context& ctx,
        std::size_t size,
        memory_kind kind = memory_kind::host)
        : data_(std::pmr::polymorphic_allocator<T>{
              ctx.state_->memory_resource()})
        , kind_(kind)
    {
        if (!__is_host_kind(kind_)) {
            throw operation_error{
                error_kind::invalid_memory_kind,
                "forge::accel::mock::host_buffer: invalid memory kind"};
        }
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

    [[nodiscard]] auto kind() const noexcept -> memory_kind {
        return kind_;
    }

private:
    [[nodiscard]] static auto __is_host_kind(memory_kind kind) noexcept -> bool {
        return kind == memory_kind::host ||
            kind == memory_kind::pinned_host ||
            kind == memory_kind::mapped_host ||
            kind == memory_kind::managed;
    }

    std::pmr::vector<T> data_;
    memory_kind kind_ = memory_kind::host;
};

template<class T>
class device_buffer {
    static_assert(std::is_trivially_copyable_v<T>,
                  "forge::accel::mock::device_buffer<T> requires trivially copyable T");

public:
    using value_type = T;

    device_buffer(
        context& ctx,
        std::size_t size,
        memory_kind kind = memory_kind::device)
        : data_(std::pmr::polymorphic_allocator<T>{
              ctx.state_->memory_resource()})
        , kind_(kind)
    {
        if (!__is_device_kind(kind_)) {
            throw operation_error{
                error_kind::invalid_memory_kind,
                "forge::accel::mock::device_buffer: invalid memory kind"};
        }
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

    [[nodiscard]] auto kind() const noexcept -> memory_kind {
        return kind_;
    }

    [[nodiscard]] bool needs_flush() const noexcept {
        return needs_flush_;
    }

    [[nodiscard]] bool needs_invalidate() const noexcept {
        return needs_invalidate_;
    }

private:
    template<class U>
    friend auto copy_to_device(queue&, device_buffer<U>&, std::span<const U>);
    template<class U>
    friend auto copy_to_device(queue&, device_buffer<U>&, const host_buffer<U>&);
    template<class U>
    friend auto copy_to_host(queue&, std::span<U>, const device_buffer<U>&);
    template<class U>
    friend auto copy_to_host(queue&, host_buffer<U>&, const device_buffer<U>&);
    template<class U>
    friend auto copy_device_to_device(queue&, device_buffer<U>&, const device_buffer<U>&);
    template<class U>
    friend auto flush(queue&, device_buffer<U>&);
    template<class U>
    friend auto invalidate(queue&, device_buffer<U>&);

    [[nodiscard]] static auto __is_device_kind(memory_kind kind) noexcept -> bool {
        return kind == memory_kind::device ||
            kind == memory_kind::cached_device ||
            kind == memory_kind::managed;
    }

    [[nodiscard]] auto __is_cached() const noexcept -> bool {
        return kind_ == memory_kind::cached_device;
    }

    void __mark_host_write() noexcept {
        if (__is_cached()) {
            needs_flush_ = true;
            needs_invalidate_ = false;
        }
    }

    void __mark_device_write() noexcept {
        if (__is_cached()) {
            needs_invalidate_ = true;
            needs_flush_ = false;
        }
    }

    void __require_readable(const char* what) const {
        if (__is_cached() && (needs_flush_ || needs_invalidate_)) {
            throw operation_error{error_kind::coherence_required, what};
        }
    }

    std::pmr::vector<T> data_;
    memory_kind kind_ = memory_kind::device;
    bool needs_flush_ = false;
    bool needs_invalidate_ = false;
};

using host_byte_buffer = host_buffer<std::byte>;
using device_byte_buffer = device_buffer<std::byte>;

template<class T>
auto copy_to_device(queue& q, device_buffer<T>& dst, std::span<const T> src) {
    return __detail::__make_command_sender(
        q.queue_.lock(),
        [dst = &dst, src] {
            if (!dst) {
                throw operation_error{
                    error_kind::invalid_buffer,
                    "forge::accel::mock::copy_to_device: null destination"};
            }
            if (dst->data_.size() != src.size()) {
                throw operation_error{
                    error_kind::size_mismatch,
                    "forge::accel::mock::copy_to_device: size mismatch"};
            }
            std::copy(src.begin(), src.end(), dst->data_.begin());
            dst->__mark_host_write();
        });
}

template<class T>
auto copy_to_device(queue& q, device_buffer<T>& dst, std::span<T> src) {
    return copy_to_device(q, dst, std::span<const T>{src});
}

template<class T>
auto copy_to_device(queue& q, device_buffer<T>& dst, const host_buffer<T>& src) {
    return copy_to_device(q, dst, src.span());
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
auto copy_to_device_typed(queue& q, device_buffer<T>& dst, const host_buffer<T>& src) {
    return __typed_detail::void_sender(copy_to_device(q, dst, src));
}

template<class T>
auto copy_to_host(queue& q, std::span<T> dst, const device_buffer<T>& src) {
    return __detail::__make_command_sender(
        q.queue_.lock(),
        [dst, src = &src] {
            if (!src) {
                throw operation_error{
                    error_kind::invalid_buffer,
                    "forge::accel::mock::copy_to_host: null source"};
            }
            if (dst.size() != src->data_.size()) {
                throw operation_error{
                    error_kind::size_mismatch,
                    "forge::accel::mock::copy_to_host: size mismatch"};
            }
            src->__require_readable(
                "forge::accel::mock::copy_to_host: cached buffer requires flush/invalidate");
            std::copy(src->data_.begin(), src->data_.end(), dst.begin());
        });
}

template<class T>
auto copy_to_host(queue& q, host_buffer<T>& dst, const device_buffer<T>& src) {
    return copy_to_host(q, dst.span(), src);
}

template<class T>
auto copy_to_host_typed(queue& q, std::span<T> dst, const device_buffer<T>& src) {
    return __typed_detail::void_sender(copy_to_host(q, dst, src));
}

template<class T>
auto copy_to_host_typed(queue& q, host_buffer<T>& dst, const device_buffer<T>& src) {
    return __typed_detail::void_sender(copy_to_host(q, dst, src));
}

template<class T>
auto copy_device_to_device(queue& q, device_buffer<T>& dst, const device_buffer<T>& src) {
    return __detail::__make_command_sender(
        q.queue_.lock(),
        [dst = &dst, src = &src] {
            if (!dst || !src) {
                throw operation_error{
                    error_kind::invalid_buffer,
                    "forge::accel::mock::copy_device_to_device: null buffer"};
            }
            if (dst->data_.size() != src->data_.size()) {
                throw operation_error{
                    error_kind::size_mismatch,
                    "forge::accel::mock::copy_device_to_device: size mismatch"};
            }
            src->__require_readable(
                "forge::accel::mock::copy_device_to_device: cached source requires flush/invalidate");
            std::copy(src->data_.begin(), src->data_.end(), dst->data_.begin());
            dst->__mark_device_write();
        });
}

template<class T>
auto copy_device_to_device_typed(
    queue& q,
    device_buffer<T>& dst,
    const device_buffer<T>& src) {
    return __typed_detail::void_sender(copy_device_to_device(q, dst, src));
}

template<class T>
auto flush(queue& q, device_buffer<T>& buffer) {
    return __detail::__make_command_sender(
        q.queue_.lock(),
        [buffer = &buffer] {
            buffer->needs_flush_ = false;
        });
}

template<class T>
auto flush_typed(queue& q, device_buffer<T>& buffer) {
    return __typed_detail::void_sender(flush(q, buffer));
}

template<class T>
auto invalidate(queue& q, device_buffer<T>& buffer) {
    return __detail::__make_command_sender(
        q.queue_.lock(),
        [buffer = &buffer] {
            buffer->needs_invalidate_ = false;
        });
}

template<class T>
auto invalidate_typed(queue& q, device_buffer<T>& buffer) {
    return __typed_detail::void_sender(invalidate(q, buffer));
}

template<class F>
auto submit(queue& q, F&& command) {
    return __detail::__make_command_sender(
        q.queue_.lock(),
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
            auto device = session_state->device.lock();
            if (!device || !device->available()) {
                throw operation_error{
                    error_kind::invalid_context,
                    "forge::accel::mock::submit(session): device is not available"};
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
                if (status != command_status::ok) {
                    __detail::__throw_for_command_status(status);
                }
            } else {
                std::invoke(handler, request, *response);
            }
        });
}

template<class Request, class Response, class Handler>
auto submit_packet(
    device_session& session,
    command_packet<Request, Response> packet,
    Handler&& handler,
    command_options options) {
    using packet_t = command_packet<Request, Response>;
    auto queue_state = session.queue_.queue_.lock();
    auto ctx = queue_state ? queue_state->owner.lock() : nullptr;
    std::shared_ptr<packet_t> packet_state;
    if (ctx) {
        packet_state = std::allocate_shared<packet_t>(
            std::pmr::polymorphic_allocator<packet_t>(ctx->memory_resource()),
            std::move(packet));
    } else {
        packet_state = std::make_shared<packet_t>(std::move(packet));
    }
    return __detail::__packet_sender<packet_t, std::decay_t<Handler>>{
        std::move(queue_state),
        session.session_,
        std::move(packet_state),
        static_cast<Handler&&>(handler),
        options};
}

template<class Request, class Response, class Handler>
auto submit_packet(
    device_session& session,
    command_packet<Request, Response> packet,
    Handler&& handler) {
    return submit_packet(
        session,
        std::move(packet),
        static_cast<Handler&&>(handler),
        command_options{});
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

template<class Request, class Response, class Handler>
auto submit_packet_typed(
    device_session& session,
    command_packet<Request, Response> packet,
    Handler&& handler,
    command_options options) {
    using packet_t = command_packet<Request, Response>;
    return __typed_detail::value_sender<packet_t>(
        submit_packet(
            session,
            std::move(packet),
            static_cast<Handler&&>(handler),
            options));
}

template<class Request, class Response, class Handler>
auto submit_packet_typed(
    device_session& session,
    command_packet<Request, Response> packet,
    Handler&& handler) {
    return submit_packet_typed(
        session,
        std::move(packet),
        static_cast<Handler&&>(handler),
        command_options{});
}

inline auto record_event(queue& q, event ev) {
    return __detail::__event_record_sender{
        q.queue_.lock(),
        std::move(ev.state_)};
}

inline auto record_event_typed(queue& q, event ev) {
    return __typed_detail::void_sender(record_event(q, std::move(ev)));
}

inline auto query_event(event ev) {
    return __detail::__event_query_sender{std::move(ev.state_)};
}

inline auto query_event_typed(event ev) {
    return __typed_detail::value_sender<event_snapshot>(
        query_event(std::move(ev)));
}

inline auto wait_event(queue& q, event ev, event_wait_options options) {
    return __detail::__event_wait_sender{
        q.queue_.lock(),
        std::move(ev.state_),
        options,
        false};
}

inline auto wait_event(queue& q, event ev) {
    return wait_event(q, std::move(ev), event_wait_options{});
}

inline auto wait_event_typed(queue& q, event ev, event_wait_options options) {
    return __typed_detail::void_sender(wait_event(q, std::move(ev), options));
}

inline auto wait_event_typed(queue& q, event ev) {
    return wait_event_typed(q, std::move(ev), event_wait_options{});
}

inline auto synchronize_event(queue& q, event ev, event_wait_options options) {
    return __detail::__event_wait_sender{
        q.queue_.lock(),
        std::move(ev.state_),
        options,
        true};
}

inline auto synchronize_event(queue& q, event ev) {
    return synchronize_event(q, std::move(ev), event_wait_options{});
}

inline auto synchronize_event_typed(queue& q, event ev, event_wait_options options) {
    return __typed_detail::void_sender(synchronize_event(q, std::move(ev), options));
}

inline auto synchronize_event_typed(queue& q, event ev) {
    return synchronize_event_typed(q, std::move(ev), event_wait_options{});
}

inline auto fence(queue& q) {
    return __detail::__make_command_sender(q.queue_.lock(), [] {});
}

inline auto fence_typed(queue& q) {
    return __typed_detail::void_sender(fence(q));
}

} // namespace forge::accel::mock
