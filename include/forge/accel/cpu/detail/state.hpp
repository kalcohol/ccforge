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

// Internal state and command-sender machinery for forge::accel::cpu.
// This header is included only after context.hpp declares the public CPU vocabulary.

namespace __detail {

using __void_completion_signatures = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;

struct __state;
inline thread_local __state* __current_state = nullptr;
inline std::atomic<std::uint64_t> __next_context_id{1};

struct __stopped_signal {};

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

    [[nodiscard]] auto wait_target_generation() const noexcept -> event_generation {
        std::lock_guard lk{mtx};
        return event_generation{record_generation == 0 ? 1 : record_generation};
    }

    [[nodiscard]] auto snapshot() const noexcept -> event_snapshot {
        std::lock_guard lk{mtx};
        return event_snapshot{
            event_generation{record_generation},
            event_generation{completed_generation},
            record_generation != 0 && completed_generation >= record_generation};
    }

    mutable std::mutex mtx;
    std::condition_variable cv;
    std::uint64_t record_generation = 0;
    std::uint64_t completed_generation = 0;
};

struct __device_state {
    explicit __device_state(device_info info) noexcept
        : info(info)
    {}

    device_info info{};
};

struct __queue_state {
    __queue_state(
        std::shared_ptr<__state> owner,
        queue_kind kind,
        stream_id stream,
        std::shared_ptr<__device_state> device);

    std::weak_ptr<__state> owner;
    queue_kind kind = queue_kind::general;
    stream_id stream{};
    std::weak_ptr<__device_state> device;
    strand lane;
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
        , devices(std::pmr::polymorphic_allocator<std::shared_ptr<__device_state>>{memory})
        , queues(std::pmr::polymorphic_allocator<std::shared_ptr<__queue_state>>{memory})
    {
        const std::size_t count = options.device_count == 0 ? 1 : options.device_count;
        for (std::size_t i = 0; i < count; ++i) {
            devices.push_back(std::allocate_shared<__device_state>(
                std::pmr::polymorphic_allocator<__device_state>{memory},
                device_info{.id = device_id{static_cast<std::uint32_t>(i)},
                            .ordinal = static_cast<std::uint32_t>(i),
                            .available = true}));
        }
    }

    ~__state() noexcept {
        shutdown();
        wait();
    }

    __state(const __state&) = delete;
    __state& operator=(const __state&) = delete;

    [[nodiscard]] bool try_accept() noexcept {
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
        std::lock_guard lk{mtx};
        stop_requested = true;
        if (pending == 0) {
            cv.notify_all();
        }
    }

    void shutdown() noexcept {
        close();
        request_stop();
        runtime.shutdown();
    }

    void wait() noexcept {
        if (__current_state == this) {
            return;
        }
        for (auto& q : snapshot_queues()) {
            q->lane.wait();
        }
        runtime.wait();
        std::unique_lock lk{mtx};
        cv.wait(lk, [this] { return pending == 0; });
        lk.unlock();
        for (auto& q : snapshot_queues()) {
            q->lane.wait();
        }
        runtime.wait();
    }

    [[nodiscard]] bool stop_requested_now() const noexcept {
        std::lock_guard lk{mtx};
        return stop_requested;
    }

    [[nodiscard]] bool closed_now() const noexcept {
        std::lock_guard lk{mtx};
        return closed || stop_requested;
    }

    [[nodiscard]] auto next_stream_id() noexcept -> stream_id {
        return stream_id{next_stream.fetch_add(1, std::memory_order_relaxed)};
    }

    [[nodiscard]] auto make_queue(
        queue_kind kind,
        std::shared_ptr<__device_state> device = nullptr)
        -> std::shared_ptr<__queue_state> {
        auto self = shared_from_this();
        auto q = std::allocate_shared<__queue_state>(
            std::pmr::polymorphic_allocator<__queue_state>{memory},
            self,
            kind,
            next_stream_id(),
            std::move(device));
        {
            std::lock_guard lk{mtx};
            queues.push_back(q);
        }
        return q;
    }

    [[nodiscard]] auto snapshot_queues()
        -> std::pmr::vector<std::shared_ptr<__queue_state>> {
        std::lock_guard lk{mtx};
        return queues;
    }

    [[nodiscard]] auto get_device(device_id dev) const
        -> std::shared_ptr<__device_state> {
        const auto index = static_cast<std::size_t>(dev.value);
        if (index >= devices.size()) {
            throw operation_error{
                error_kind::invalid_context,
                "forge::accel::cpu: invalid device id"};
        }
        return devices[index];
    }

    context_id id{};
    std::pmr::memory_resource* memory = forge::default_memory_resource();
    resource_context runtime;
    std::optional<std::size_t> queue_capacity;
    std::atomic<std::uint64_t> next_stream{1};
    mutable std::mutex mtx;
    std::condition_variable cv;
    std::size_t pending = 0;
    bool closed = false;
    bool stop_requested = false;
    std::pmr::vector<std::shared_ptr<__device_state>> devices;
    std::pmr::vector<std::shared_ptr<__queue_state>> queues;
};

inline __queue_state::__queue_state(
    std::shared_ptr<__state> owner_arg,
    queue_kind kind_arg,
    stream_id stream_arg,
    std::shared_ptr<__device_state> device_arg)
    : owner(owner_arg)
    , kind(kind_arg)
    , stream(stream_arg)
    , device(std::move(device_arg))
    , lane(owner_arg->runtime.get_scheduler(), strand_options{.memory = owner_arg->memory})
{}

[[nodiscard]] inline auto wait_until_event_ready_or_stopped(
    const std::shared_ptr<__state>& state,
    const std::shared_ptr<__event_state>& ev,
    event_generation target,
    std::optional<std::chrono::steady_clock::time_point> deadline) noexcept
    -> command_status {
    std::unique_lock lk{ev->mtx};
    for (;;) {
        if (ev->completed_generation >= target.value) {
            return command_status::ok;
        }
        if (!state || state->stop_requested_now()) {
            return command_status::stopped;
        }
        if (deadline && std::chrono::steady_clock::now() >= *deadline) {
            return command_status::timed_out;
        }
        if (deadline) {
            ev->cv.wait_until(lk, std::min(*deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds{1}));
        } else {
            ev->cv.wait_for(lk, std::chrono::milliseconds{1});
        }
    }
}

template<class R>
[[nodiscard]] bool __stop_requested(const R& rcvr) noexcept {
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
            if (state->stop_requested_now()) {
                throw __stopped_signal{};
            }
            std::invoke(std::move(action));
            state->finish_one();
            std::execution::set_value(std::move(rcvr));
        } catch (const __stopped_signal&) {
            state->finish_one();
            std::execution::set_stopped(std::move(rcvr));
        } catch (...) {
            auto ep = std::current_exception();
            state->finish_one();
            std::execution::set_error(std::move(rcvr), std::move(ep));
        }
    }

    void set_error(std::exception_ptr ep) && noexcept {
        state->finish_one();
        std::execution::set_error(std::move(rcvr), std::move(ep));
    }

    void set_stopped() && noexcept {
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
        using scheduler_t = strand::scheduler;
        using schedule_sender_t = decltype(std::execution::schedule(
            std::declval<scheduler_t>()));
        using receiver_t = __command_receiver<R, Action>;
        using op_t = std::execution::connect_result_t<schedule_sender_t, receiver_t>;

        __op(std::shared_ptr<__queue_state> q, Action a, R r)
            : queue(std::move(q))
            , action(std::move(a))
            , rcvr(std::move(r))
        {}

        __op(__op&&) = delete;
        __op& operator=(__op&&) = delete;
        __op(const __op&) = delete;
        __op& operator=(const __op&) = delete;

        void start() & noexcept {
            auto state = queue ? queue->owner.lock() : nullptr;
            if (!state || __stop_requested(*rcvr)) {
                std::execution::set_stopped(std::move(*rcvr));
                return;
            }
            if (!state->try_accept()) {
                std::execution::set_stopped(std::move(*rcvr));
                return;
            }
            try {
                auto sender = std::execution::schedule(queue->lane.get_scheduler());
                auto* op = inner.emplace_from([&] {
                    return std::execution::connect(
                        std::move(sender),
                        receiver_t{state, std::move(*rcvr), std::move(action)});
                });
                rcvr.reset();
                std::execution::start(*op);
            } catch (...) {
                auto ep = std::current_exception();
                state->finish_one();
                std::execution::set_error(std::move(*rcvr), std::move(ep));
            }
        }

        std::shared_ptr<__queue_state> queue;
        Action action;
        std::optional<R> rcvr;
        __op_box<op_t> inner;
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

template<class Action>
[[nodiscard]] auto make_command_sender(
    std::shared_ptr<__queue_state> queue,
    Action&& action) -> __command_sender<std::decay_t<Action>> {
    return __command_sender<std::decay_t<Action>>{
        std::move(queue),
        static_cast<Action&&>(action)};
}

} // namespace __detail
