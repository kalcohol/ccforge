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

// Internal implementation detail for forge::accel::mock::context.
// This header is included only after context.hpp declares the public mock vocabulary.

namespace forge::accel::mock::__detail {


using __void_completion_signatures = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;

struct __state;
inline thread_local __state* __current_state = nullptr;
inline std::atomic<std::uint64_t> __next_context_id{1};

struct __stopped_signal {};

struct __device_state;

struct __worker_lifecycle_snapshot {
    worker_generation generation{};
    bool drain_frozen = false;
    bool worker_faulted = false;
    bool host_lost = false;
};

struct __session_state {
    std::weak_ptr<__device_state> device;
    session_id id{};
    device_epoch epoch{};
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
        : id(context_id{__next_context_id.fetch_add(1, std::memory_order_relaxed)})
        , memory(normalize_memory_resource(options.memory))
        , runtime(resource_context_options{
              .thread_count = options.thread_count == 0 ? 1 : options.thread_count,
              .queue_capacity = std::nullopt,
              .memory = memory,
          })
        , queue_capacity(options.queue_capacity)
        , device_count(options.device_count)
        , trace(options.trace)
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

    [[nodiscard]] auto next_stream_id() noexcept -> stream_id {
        return stream_id{next_stream.fetch_add(1, std::memory_order_relaxed)};
    }

    void record_trace(trace_event event) noexcept {
        if (!trace) {
            return;
        }
        event.context = id;
        trace->record(event);
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

    context_id id{};
    std::pmr::memory_resource* memory;
    resource_context runtime;
    std::optional<std::size_t> queue_capacity;
    std::size_t device_count = 1;
    trace_sink* trace = nullptr;
    std::atomic<std::uint64_t> next_stream{1};
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

    [[nodiscard]] bool lost_now() const noexcept {
        return lost.load(std::memory_order_acquire);
    }

    [[nodiscard]] auto current_epoch() const noexcept -> device_epoch {
        return device_epoch{epoch.load(std::memory_order_acquire)};
    }

    [[nodiscard]] auto next_session_id() noexcept -> session_id {
        return session_id{next_session.fetch_add(1, std::memory_order_acq_rel)};
    }

    [[nodiscard]] auto worker_snapshot() const noexcept
        -> __worker_lifecycle_snapshot {
        std::lock_guard lk{worker_mtx};
        return __worker_lifecycle_snapshot{
            worker_generation{worker_generation_value},
            drain_frozen,
            worker_faulted,
            host_lost};
    }

    void note_heartbeat() noexcept {
        std::lock_guard lk{worker_mtx};
        last_heartbeat = std::chrono::steady_clock::now();
    }

    [[nodiscard]] bool mark_worker_fault_if_heartbeat_expired(
        std::chrono::steady_clock::duration timeout) noexcept {
        std::lock_guard lk{worker_mtx};
        if (std::chrono::steady_clock::now() - last_heartbeat < timeout) {
            return false;
        }
        worker_faulted = true;
        return true;
    }

    void begin_drain_freeze() noexcept {
        std::lock_guard lk{worker_mtx};
        drain_frozen = true;
    }

    void complete_drain() noexcept {
        std::lock_guard lk{worker_mtx};
        if (drain_frozen) {
            drain_frozen = false;
            ++worker_generation_value;
        }
    }

    void mark_worker_fault() noexcept {
        std::lock_guard lk{worker_mtx};
        worker_faulted = true;
    }

    [[nodiscard]] bool clear_worker_fault(worker_generation generation) noexcept {
        std::lock_guard lk{worker_mtx};
        if (generation.value != worker_generation_value || !worker_faulted) {
            return false;
        }
        worker_faulted = false;
        ++worker_generation_value;
        return true;
    }

    void begin_host_lost_cleanup() noexcept {
        std::lock_guard lk{worker_mtx};
        host_lost = true;
        drain_frozen = true;
    }

    void complete_host_lost_cleanup() noexcept {
        std::lock_guard lk{worker_mtx};
        if (!host_lost) {
            return;
        }
        host_lost = false;
        drain_frozen = false;
        worker_faulted = false;
        ++worker_generation_value;
        epoch.fetch_add(1, std::memory_order_acq_rel);
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
        epoch.fetch_add(1, std::memory_order_acq_rel);
    }

    std::weak_ptr<__state> owner;
    device_info info{};
    std::atomic<bool> lost{false};
    std::atomic<std::uint64_t> epoch{1};
    std::atomic<std::uint64_t> next_session{1};
    mutable std::mutex worker_mtx;
    std::uint64_t worker_generation_value = 1;
    std::chrono::steady_clock::time_point last_heartbeat =
        std::chrono::steady_clock::now();
    bool drain_frozen = false;
    bool worker_faulted = false;
    bool host_lost = false;
};

struct __queue_state {
    __queue_state(
        std::shared_ptr<__state> owner_state,
        queue_kind queue_kind,
        std::shared_ptr<__device_state> bound_device = nullptr)
        : owner(owner_state)
        , device(std::move(bound_device))
        , stream(owner_state->next_stream_id())
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
    stream_id stream{};
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

struct __trace_error_info {
    trace_event_kind kind = trace_event_kind::error;
    error_kind error = error_kind::unknown;
    command_status status = command_status::failed;
};

[[nodiscard]] inline auto __trace_kind_for_error(error_kind kind) noexcept
    -> trace_event_kind {
    if (kind == error_kind::timeout) {
        return trace_event_kind::timeout;
    }
    if (kind == error_kind::device_lost) {
        return trace_event_kind::device_lost;
    }
    if (kind == error_kind::stale_session) {
        return trace_event_kind::session_stale;
    }
    return trace_event_kind::error;
}

[[nodiscard]] inline auto __trace_error_from_exception(
    std::exception_ptr ep) noexcept -> __trace_error_info {
    if (!ep) {
        return {};
    }
    try {
        std::rethrow_exception(ep);
    } catch (const operation_error& e) {
        return __trace_error_info{
            __trace_kind_for_error(e.kind()),
            e.kind(),
            e.status()};
    } catch (const command_error& e) {
        auto kind = error_kind::command_failed;
        if (e.status() == command_status::timed_out) {
            kind = error_kind::timeout;
        } else if (e.status() == command_status::aborted) {
            kind = error_kind::aborted;
        }
        return __trace_error_info{
            __trace_kind_for_error(kind),
            kind,
            e.status()};
    } catch (...) {
        return __trace_error_info{
            trace_event_kind::error,
            error_kind::user_exception,
            command_status::failed};
    }
}

inline void __fill_trace_device(
    trace_event& event,
    const std::shared_ptr<__device_state>& device) noexcept {
    if (!device) {
        return;
    }
    event.device = device->info.id;
    event.epoch = device->current_epoch();
    event.worker = device->worker_snapshot().generation;
}

[[nodiscard]] inline auto __make_trace_event(
    const std::shared_ptr<__queue_state>& queue,
    const std::shared_ptr<__session_state>& session,
    trace_event_kind kind,
    std::optional<worker_generation> accepted_generation = std::nullopt,
    command_id command = {}) noexcept -> trace_event {
    trace_event event{};
    event.kind = kind;
    event.command = command;
    if (queue) {
        event.stream = queue->stream;
    }
    if (session) {
        event.session = session->id;
        event.epoch = session->epoch;
        auto device = session->device.lock();
        __fill_trace_device(event, device);
    } else if (queue) {
        __fill_trace_device(event, queue->device);
    }
    if (accepted_generation) {
        event.worker = *accepted_generation;
    }
    return event;
}

inline void __record_trace_exception(
    const std::shared_ptr<__state>& state,
    const std::shared_ptr<__queue_state>& queue,
    const std::shared_ptr<__session_state>& session,
    std::optional<worker_generation> accepted_generation,
    command_id command,
    std::exception_ptr ep) noexcept {
    if (!state) {
        return;
    }
    auto event = __make_trace_event(
        queue,
        session,
        trace_event_kind::error,
        accepted_generation,
        command);
    auto info = __trace_error_from_exception(std::move(ep));
    event.kind = info.kind;
    event.error = info.error;
    event.status = info.status;
    state->record_trace(event);
}

inline void __record_trace_lifecycle(
    const std::shared_ptr<__device_state>& device,
    trace_event_kind kind,
    error_kind error = error_kind::unknown) noexcept {
    if (!device) {
        return;
    }
    auto state = device->owner.lock();
    if (!state) {
        return;
    }
    trace_event event{};
    event.kind = kind;
    event.error = error;
    event.status = command_status::ok;
    __fill_trace_device(event, device);
    state->record_trace(event);
}

inline auto __admit_device_for_command(
    const std::shared_ptr<__device_state>& device,
    const char* what) -> worker_generation {
    if (!device) {
        throw operation_error{error_kind::invalid_context, what};
    }
    if (device->lost_now()) {
        throw operation_error{error_kind::device_lost, what};
    }
    if (!device->available()) {
        throw operation_error{error_kind::invalid_context, what};
    }
    auto snapshot = device->worker_snapshot();
    if (snapshot.host_lost) {
        throw operation_error{error_kind::host_lost, what};
    }
    if (snapshot.drain_frozen) {
        throw operation_error{error_kind::drain_freeze, what};
    }
    if (snapshot.worker_faulted) {
        throw operation_error{error_kind::worker_fault, what};
    }
    return snapshot.generation;
}

inline void __validate_device_for_execution(
    const std::shared_ptr<__device_state>& device,
    std::optional<worker_generation> accepted_generation,
    const char* what) {
    if (!device) {
        throw operation_error{error_kind::invalid_context, what};
    }
    if (device->lost_now()) {
        throw operation_error{error_kind::device_lost, what};
    }
    if (!device->available()) {
        throw operation_error{error_kind::invalid_context, what};
    }
    auto snapshot = device->worker_snapshot();
    if (snapshot.host_lost) {
        throw operation_error{error_kind::host_lost, what};
    }
    if (snapshot.worker_faulted) {
        throw operation_error{error_kind::worker_fault, what};
    }
    if (accepted_generation && snapshot.generation != *accepted_generation) {
        throw operation_error{error_kind::stale_session, what};
    }
}

inline auto __admit_queue_for_command(
    const std::shared_ptr<__queue_state>& queue,
    const char* what) -> std::optional<worker_generation> {
    if (!queue || !queue->device) {
        return std::nullopt;
    }
    return __admit_device_for_command(queue->device, what);
}

inline void __validate_queue_for_execution(
    const std::shared_ptr<__queue_state>& queue,
    std::optional<worker_generation> accepted_generation,
    const char* what) {
    if (!queue || !queue->device) {
        return;
    }
    __validate_device_for_execution(queue->device, accepted_generation, what);
}

inline auto __admit_session_for_command(
    const std::shared_ptr<__session_state>& session,
    const char* what) -> worker_generation {
    if (!session ||
        session->reset_requested.load(std::memory_order_acquire)) {
        throw __stopped_signal{};
    }
    auto device = session->device.lock();
    if (!device || device->current_epoch() != session->epoch) {
        throw operation_error{error_kind::stale_session, what};
    }
    auto generation = __admit_device_for_command(device, what);
    if (device->current_epoch() != session->epoch) {
        throw operation_error{error_kind::stale_session, what};
    }
    return generation;
}

inline void __validate_session_for_execution(
    const std::shared_ptr<__session_state>& session,
    worker_generation accepted_generation,
    const char* what) {
    if (!session ||
        session->reset_requested.load(std::memory_order_acquire)) {
        throw __stopped_signal{};
    }
    auto device = session->device.lock();
    if (!device || device->current_epoch() != session->epoch) {
        throw operation_error{error_kind::stale_session, what};
    }
    __validate_device_for_execution(device, accepted_generation, what);
    if (device->current_epoch() != session->epoch) {
        throw operation_error{error_kind::stale_session, what};
    }
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

} // namespace forge::accel::mock::__detail
