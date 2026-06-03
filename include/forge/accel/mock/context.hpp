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
#include "../protocol.hpp"
#include "../vocabulary.hpp"
#include "../../resource_context.hpp"
#include "../../resource_policy.hpp"
#include "../../strand.hpp"

#include <execution>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
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

enum class trace_event_kind {
    submitted,
    started,
    completed,
    stopped,
    error,
    timeout,
    device_lost,
    session_stale,
    lifecycle_signal
};

struct trace_event {
    std::uint64_t sequence = 0;
    std::chrono::steady_clock::time_point timestamp{};
    std::chrono::steady_clock::time_point end_timestamp{};
    bool has_end_timestamp = false;
    trace_event_kind kind = trace_event_kind::submitted;
    context_id context{};
    device_id device{};
    session_id session{};
    stream_id stream{};
    request_id request{};
    module_id module{};
    command_id command{};
    event_generation generation{};
    device_epoch epoch{};
    worker_generation worker{};
    error_kind error = error_kind::unknown;
    command_status status = command_status::ok;
};

class trace_sink {
public:
    explicit trace_sink(
        std::pmr::memory_resource* memory = forge::default_memory_resource())
        : events_(std::pmr::polymorphic_allocator<trace_event>{
              normalize_memory_resource(memory)})
    {}

    void record(trace_event event) noexcept {
        try {
            if (event.timestamp == std::chrono::steady_clock::time_point{}) {
                event.timestamp = std::chrono::steady_clock::now();
            }
            std::lock_guard lk{mtx_};
            event.sequence = next_sequence_++;
            events_.push_back(event);
        } catch (...) {
        }
    }

    [[nodiscard]] auto snapshot() const -> std::vector<trace_event> {
        std::lock_guard lk{mtx_};
        return {events_.begin(), events_.end()};
    }

    void clear() noexcept {
        try {
            std::lock_guard lk{mtx_};
            events_.clear();
        } catch (...) {
        }
    }

private:
    mutable std::mutex mtx_;
    std::uint64_t next_sequence_ = 1;
    std::pmr::vector<trace_event> events_;
};

struct context_options {
    std::size_t thread_count = 1;
    std::optional<std::size_t> queue_capacity = std::nullopt;
    std::size_t device_count = 1;
    std::pmr::memory_resource* memory = forge::default_memory_resource();
    trace_sink* trace = nullptr;
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

struct stream_sync_options {
    std::optional<std::chrono::steady_clock::duration> timeout = std::nullopt;
    bool clear_sticky_error = true;
};

struct stream_snapshot {
    stream_id stream{};
    queue_kind kind = queue_kind::general;
    bool closed = true;
    bool idle = true;
    std::size_t pending_nodes = 0;
    bool has_sticky_error = false;
    error sticky_error{};
};

struct stream_sync_result {
    stream_snapshot snapshot{};
    command_status status = command_status::ok;
    bool has_error = false;
    error sticky_error{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == command_status::ok && !has_error;
    }
};

template<class Request, class Response>
struct command_packet {
    using request_type = Request;
    using response_type = Response;

    command_packet(command_id id, Request request, Response response)
        : command_packet(module_id{}, id, std::move(request), std::move(response))
    {}

    command_packet(module_id module, command_id id, Request request, Response response)
        : module(module)
        , id(id)
        , request(std::move(request))
        , response(std::move(response))
    {}

    module_id module{};
    command_id id{};
    Request request;
    Response response;
    command_status status = command_status::ok;
};

template<class Request, class Response>
command_packet(command_id, Request, Response) -> command_packet<Request, Response>;

template<class Request, class Response>
command_packet(module_id, command_id, Request, Response) -> command_packet<Request, Response>;

template<class Request, class Response>
class command_dispatcher {
public:
    using request_type = Request;
    using response_type = Response;
    using handler_type = std::function<command_status(Request&, Response&)>;

    explicit command_dispatcher(
        std::pmr::memory_resource* memory = forge::default_memory_resource())
        : entries_(std::pmr::polymorphic_allocator<entry>{normalize_memory_resource(memory)})
    {}

    template<class Handler>
    void register_handler(module_id module, command_id command, Handler&& handler) {
        handler_type wrapped{
            [handler = std::forward<Handler>(handler)](
                Request& request,
                Response& response) mutable -> command_status {
                using result_t = std::invoke_result_t<Handler&, Request&, Response&>;
                if constexpr (std::is_same_v<result_t, command_status>) {
                    return std::invoke(handler, request, response);
                } else {
                    std::invoke(handler, request, response);
                    return command_status::ok;
                }
            }};

        std::lock_guard lk{mtx_};
        for (auto& item : entries_) {
            if (item.module == module && item.command == command) {
                item.handler = std::move(wrapped);
                return;
            }
        }
        entries_.push_back(entry{module, command, std::move(wrapped)});
    }

    [[nodiscard]] auto invoke(
        module_id module,
        command_id command,
        Request& request,
        Response& response) const -> command_status {
        handler_type handler;
        {
            std::lock_guard lk{mtx_};
            for (const auto& item : entries_) {
                if (item.module == module && item.command == command) {
                    handler = item.handler;
                    break;
                }
            }
        }
        if (!handler) {
            throw operation_error{
                error_kind::protocol_error,
                "forge::accel::mock::command_dispatcher: no handler"};
        }
        return handler(request, response);
    }

private:
    struct entry {
        module_id module{};
        command_id command{};
        handler_type handler;
    };

    mutable std::mutex mtx_;
    std::pmr::vector<entry> entries_;
};

struct event_snapshot {
    event_generation record_generation{};
    event_generation completed_generation{};
    bool ready = false;
};

} // namespace forge::accel::mock

#include "detail/state.hpp"
#include "detail/command.hpp"

namespace forge::accel::mock {

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
    friend auto elapsed_time(event) -> std::chrono::steady_clock::duration;
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
    template<class F>
    friend auto submit(device_session&, F&&);
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
    friend auto query_stream(queue&) noexcept -> stream_snapshot;
    friend auto synchronize_stream(queue&, stream_sync_options) noexcept
        -> stream_sync_result;
    friend auto synchronize_stream(queue&) noexcept -> stream_sync_result;

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
            auto device = session_->device.lock();
            auto state = device ? device->owner.lock() : nullptr;
            if (state) {
                trace_event event{};
                event.kind = trace_event_kind::lifecycle_signal;
                event.session = session_->id;
                event.epoch = session_->epoch;
                event.error = error_kind::stale_session;
                __detail::__fill_trace_device(event, device);
                state->record_trace(event);
            }
        }
    }

    [[nodiscard]] bool reset_requested() const noexcept {
        return !session_ ||
            session_->reset_requested.load(std::memory_order_acquire);
    }

    [[nodiscard]] auto id() const noexcept -> session_id {
        return session_ ? session_->id : session_id{};
    }

    [[nodiscard]] auto epoch() const noexcept -> device_epoch {
        return session_ ? session_->epoch : device_epoch{};
    }

private:
    explicit device_session(
        std::shared_ptr<__detail::__state> state,
        std::shared_ptr<__detail::__device_state> device)
        : queue_(state
              ? state->make_queue(queue_kind::command, device)
              : nullptr)
        , session_(state && device
              ? std::allocate_shared<__detail::__session_state>(
                    std::pmr::polymorphic_allocator<__detail::__session_state>{
                        state->memory_resource()})
              : nullptr) {
        if (session_) {
            session_->id = device->next_session_id();
            session_->epoch = device->current_epoch();
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

    [[nodiscard]] auto epoch() const noexcept -> device_epoch {
        auto device_state = device_.lock();
        return device_state ? device_state->current_epoch() : device_epoch{};
    }

    [[nodiscard]] auto current_worker_generation() const noexcept -> worker_generation {
        auto device_state = device_.lock();
        return device_state
            ? device_state->worker_snapshot().generation
            : worker_generation{};
    }

    void mark_lost() noexcept {
        if (auto device_state = device_.lock()) {
            device_state->mark_lost();
            __detail::__record_trace_lifecycle(
                device_state,
                trace_event_kind::device_lost,
                error_kind::device_lost);
        }
    }

    void reset() noexcept {
        if (auto device_state = device_.lock()) {
            device_state->reset();
            __detail::__record_trace_lifecycle(
                device_state,
                trace_event_kind::lifecycle_signal);
        }
    }

    void begin_drain_freeze() noexcept {
        if (auto device_state = device_.lock()) {
            device_state->begin_drain_freeze();
            __detail::__record_trace_lifecycle(
                device_state,
                trace_event_kind::lifecycle_signal,
                error_kind::drain_freeze);
        }
    }

    void complete_drain() noexcept {
        if (auto device_state = device_.lock()) {
            device_state->complete_drain();
            __detail::__record_trace_lifecycle(
                device_state,
                trace_event_kind::lifecycle_signal);
        }
    }

    void note_heartbeat() noexcept {
        if (auto device_state = device_.lock()) {
            device_state->note_heartbeat();
            __detail::__record_trace_lifecycle(
                device_state,
                trace_event_kind::lifecycle_signal);
        }
    }

    [[nodiscard]] bool mark_heartbeat_timeout_if_stale(
        std::chrono::steady_clock::duration timeout) noexcept {
        auto device_state = device_.lock();
        if (!device_state ||
            !device_state->mark_worker_fault_if_heartbeat_expired(timeout)) {
            return false;
        }
        __detail::__record_trace_lifecycle(
            device_state,
            trace_event_kind::lifecycle_signal,
            error_kind::worker_fault);
        return true;
    }

    void mark_worker_fault() noexcept {
        if (auto device_state = device_.lock()) {
            device_state->mark_worker_fault();
            __detail::__record_trace_lifecycle(
                device_state,
                trace_event_kind::lifecycle_signal,
                error_kind::worker_fault);
        }
    }

    [[nodiscard]] bool clear_worker_fault(worker_generation generation) noexcept {
        auto device_state = device_.lock();
        if (!device_state || !device_state->clear_worker_fault(generation)) {
            return false;
        }
        __detail::__record_trace_lifecycle(
            device_state,
            trace_event_kind::lifecycle_signal);
        return true;
    }

    void begin_host_lost_cleanup() noexcept {
        if (auto device_state = device_.lock()) {
            device_state->begin_host_lost_cleanup();
            __detail::__record_trace_lifecycle(
                device_state,
                trace_event_kind::lifecycle_signal,
                error_kind::host_lost);
        }
    }

    void complete_host_lost_cleanup() noexcept {
        if (auto device_state = device_.lock()) {
            device_state->complete_host_lost_cleanup();
            __detail::__record_trace_lifecycle(
                device_state,
                trace_event_kind::lifecycle_signal);
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
    return __detail::__make_session_command_sender(
        session.get_queue().queue_.lock(),
        session.session_,
        [command = std::forward<F>(command)]() mutable {
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

template<class Request, class Response>
auto submit_packet(
    device_session& session,
    command_packet<Request, Response> packet,
    command_dispatcher<Request, Response>& dispatcher,
    command_options options) {
    const auto module = packet.module;
    const auto command = packet.id;
    return submit_packet(
        session,
        std::move(packet),
        [&dispatcher, module, command](Request& request, Response& response) {
            return dispatcher.invoke(module, command, request, response);
        },
        options);
}

template<class Request, class Response>
auto submit_packet(
    device_session& session,
    command_packet<Request, Response> packet,
    command_dispatcher<Request, Response>& dispatcher) {
    return submit_packet(
        session,
        std::move(packet),
        dispatcher,
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

template<class Request, class Response>
auto submit_packet_typed(
    device_session& session,
    command_packet<Request, Response> packet,
    command_dispatcher<Request, Response>& dispatcher,
    command_options options) {
    using packet_t = command_packet<Request, Response>;
    return __typed_detail::value_sender<packet_t>(
        submit_packet(
            session,
            std::move(packet),
            dispatcher,
            options));
}

template<class Request, class Response>
auto submit_packet_typed(
    device_session& session,
    command_packet<Request, Response> packet,
    command_dispatcher<Request, Response>& dispatcher) {
    return submit_packet_typed(
        session,
        std::move(packet),
        dispatcher,
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

inline auto elapsed_time(event ev) -> std::chrono::steady_clock::duration {
    auto state = ev.state_;
    if (!state) {
        throw operation_error{
            error_kind::invalid_event,
            "forge::accel::mock::elapsed_time: invalid event"};
    }
    auto elapsed = state->elapsed_time();
    if (!elapsed) {
        throw operation_error{
            error_kind::invalid_event,
            "forge::accel::mock::elapsed_time: event has not completed"};
    }
    return *elapsed;
}

inline auto query_stream(queue& q) noexcept -> stream_snapshot {
    auto queue_state = q.queue_.lock();
    if (!queue_state) {
        return {};
    }
    auto state = queue_state->owner.lock();
    return queue_state->snapshot(!state || state->is_closed());
}

inline auto synchronize_stream(
    queue& q,
    stream_sync_options options) noexcept -> stream_sync_result {
    auto queue_state = q.queue_.lock();
    if (!queue_state) {
        auto out = stream_sync_result{};
        out.status = command_status::stopped;
        return out;
    }
    auto state = queue_state->owner.lock();
    if (!state || __detail::__current_state == state.get()) {
        auto out = stream_sync_result{};
        out.status = command_status::stopped;
        out.snapshot = queue_state->snapshot(!state || state->is_closed());
        return out;
    }
    std::optional<std::chrono::steady_clock::time_point> deadline;
    if (options.timeout) {
        deadline = std::chrono::steady_clock::now() + *options.timeout;
    }
    return queue_state->wait_idle(state, deadline, options.clear_sticky_error);
}

inline auto synchronize_stream(queue& q) noexcept -> stream_sync_result {
    return synchronize_stream(q, stream_sync_options{});
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
