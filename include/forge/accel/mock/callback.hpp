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

#include "context.hpp"

namespace forge::accel::mock {

struct callback_result {
    callback_id callback{};
    callback_invoke_id invoke{};
    callback_status status = callback_status::ok;
    error err{};
    std::chrono::steady_clock::time_point started{};
    std::chrono::steady_clock::time_point completed{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == callback_status::ok;
    }
};

class host_callback_dispatcher {
public:
    explicit host_callback_dispatcher(
        std::pmr::memory_resource* memory = forge::default_memory_resource())
        : memory_(normalize_memory_resource(memory))
        , runtime_(resource_context_options{
              .thread_count = 1,
              .queue_capacity = std::nullopt,
              .memory = memory_,
          })
        , lane_(runtime_.get_scheduler(), strand_options{.memory = memory_})
        , records_(
              std::pmr::polymorphic_allocator<std::shared_ptr<record>>{memory_})
        , completions_(
              std::pmr::polymorphic_allocator<callback_result>{memory_})
    {}

    ~host_callback_dispatcher() noexcept {
        shutdown();
        wait();
    }

    host_callback_dispatcher(const host_callback_dispatcher&) = delete;
    auto operator=(const host_callback_dispatcher&)
        -> host_callback_dispatcher& = delete;
    host_callback_dispatcher(host_callback_dispatcher&&) = delete;
    auto operator=(host_callback_dispatcher&&)
        -> host_callback_dispatcher& = delete;

    template<class Handler>
    [[nodiscard]] auto register_callback(Handler&& handler) -> callback_id {
        auto id = callback_id{next_callback_.fetch_add(1, std::memory_order_relaxed)};
        register_callback(id, static_cast<Handler&&>(handler));
        return id;
    }

    template<class Handler>
    void register_callback(callback_id id, Handler&& handler) {
        auto wrapped = callback_fn{
            [handler = std::forward<Handler>(handler)](
                callback_invoke_id invoke) mutable {
                if constexpr (std::is_invocable_v<Handler&, callback_invoke_id>) {
                    std::invoke(handler, invoke);
                } else {
                    std::invoke(handler);
                }
            }};

        std::lock_guard lk{mtx_};
        if (closed_) {
            throw operation_error{
                error_kind::invalid_context,
                "forge::accel::mock::host_callback_dispatcher is closed"};
        }
        for (auto& item : records_) {
            if (item && item->id == id) {
                std::lock_guard record_lk{item->mtx};
                item->fn = std::move(wrapped);
                item->registered = true;
                return;
            }
        }
        auto rec = std::allocate_shared<record>(
            std::pmr::polymorphic_allocator<record>{memory_},
            id,
            std::move(wrapped));
        records_.push_back(std::move(rec));
    }

    void unregister_callback(callback_id id) noexcept {
        auto rec = find_record(id);
        if (!rec) {
            return;
        }
        std::unique_lock lk{rec->mtx};
        rec->registered = false;
        rec->cv.wait(lk, [&] { return rec->in_flight == 0; });
    }

    void close() noexcept {
        std::lock_guard lk{mtx_};
        closed_ = true;
    }

    void request_stop() noexcept {
        close();
        lane_.shutdown();
        runtime_.request_stop();
    }

    void shutdown() noexcept {
        close();
        request_stop();
    }

    void wait() noexcept {
        lane_.wait();
        runtime_.wait();
        auto snapshot = records_snapshot();
        for (auto& rec : snapshot) {
            std::unique_lock lk{rec->mtx};
            rec->cv.wait(lk, [&] { return rec->in_flight == 0; });
        }
    }

    [[nodiscard]] auto invoke(callback_id id) -> callback_result {
        auto rec = acquire_record(id);
        auto result = callback_result{
            .callback = id,
            .invoke = callback_invoke_id{
                next_invoke_.fetch_add(1, std::memory_order_relaxed)},
            .started = std::chrono::steady_clock::now(),
        };

        if (!rec) {
            result.status = callback_status::missing;
            result.err = error{error_kind::protocol_error};
            result.completed = std::chrono::steady_clock::now();
            record_completion(result);
            return result;
        }

        try {
            auto sender = std::execution::then(
                std::execution::schedule(lane_.get_scheduler()),
                [rec, invoke = result.invoke] {
                    callback_fn fn;
                    {
                        std::lock_guard lk{rec->mtx};
                        fn = rec->fn;
                    }
                    if (!fn) {
                        throw operation_error{
                            error_kind::protocol_error,
                            "forge::accel::mock::host callback was unregistered"};
                    }
                    fn(invoke);
                });
            auto completed = std::execution::sync_wait(std::move(sender));
            if (!completed) {
                result.status = callback_status::stopped;
                result.err = error{error_kind::aborted, command_status::stopped};
            }
        } catch (...) {
            result.status = callback_status::failed;
            result.err = __typed_detail::from_exception(std::current_exception());
        }

        result.completed = std::chrono::steady_clock::now();
        release_record(rec);
        record_completion(result);
        return result;
    }

    [[nodiscard]] auto completions() const -> std::vector<callback_result> {
        std::lock_guard lk{mtx_};
        return {completions_.begin(), completions_.end()};
    }

private:
    using callback_fn = std::function<void(callback_invoke_id)>;

    struct record {
        record(callback_id id_arg, callback_fn fn_arg)
            : id(id_arg)
            , fn(std::move(fn_arg))
        {}

        callback_id id{};
        callback_fn fn;
        std::mutex mtx;
        std::condition_variable cv;
        std::size_t in_flight = 0;
        bool registered = true;
    };

    [[nodiscard]] auto records_snapshot() noexcept
        -> std::pmr::vector<std::shared_ptr<record>> {
        std::pmr::vector<std::shared_ptr<record>> out{
            std::pmr::polymorphic_allocator<std::shared_ptr<record>>{memory_}};
        std::lock_guard lk{mtx_};
        out = records_;
        return out;
    }

    [[nodiscard]] auto find_record(callback_id id) noexcept
        -> std::shared_ptr<record> {
        std::lock_guard lk{mtx_};
        for (auto& rec : records_) {
            if (rec && rec->id == id) {
                return rec;
            }
        }
        return {};
    }

    [[nodiscard]] auto acquire_record(callback_id id) noexcept
        -> std::shared_ptr<record> {
        auto rec = find_record(id);
        if (!rec) {
            return {};
        }
        std::lock_guard lk{rec->mtx};
        if (!rec->registered) {
            return {};
        }
        ++rec->in_flight;
        return rec;
    }

    void release_record(const std::shared_ptr<record>& rec) noexcept {
        {
            std::lock_guard lk{rec->mtx};
            if (rec->in_flight > 0) {
                --rec->in_flight;
            }
        }
        rec->cv.notify_all();
    }

    void record_completion(callback_result result) noexcept {
        try {
            std::lock_guard lk{mtx_};
            completions_.push_back(std::move(result));
        } catch (...) {
        }
    }

    std::pmr::memory_resource* memory_;
    resource_context runtime_;
    strand lane_;
    std::atomic<std::uint64_t> next_callback_{1};
    std::atomic<std::uint64_t> next_invoke_{1};
    mutable std::mutex mtx_;
    bool closed_ = false;
    std::pmr::vector<std::shared_ptr<record>> records_;
    std::pmr::vector<callback_result> completions_;
};

inline void __record_callback_trace(
    const std::shared_ptr<__detail::__queue_state>& queue,
    const callback_result& result) noexcept {
    if (!queue) {
        return;
    }
    auto state = queue->owner.lock();
    if (!state) {
        return;
    }

    auto invoke = __detail::__make_trace_event(
        queue,
        nullptr,
        trace_event_kind::callback_invoke);
    invoke.callback = result.callback;
    invoke.callback_invoke = result.invoke;
    invoke.timestamp = result.started;
    state->record_trace(invoke);

    auto complete = __detail::__make_trace_event(
        queue,
        nullptr,
        trace_event_kind::callback_complete);
    complete.callback = result.callback;
    complete.callback_invoke = result.invoke;
    complete.timestamp = result.started;
    complete.end_timestamp = result.completed;
    complete.has_end_timestamp = true;
    complete.error = result.err.kind;
    complete.status = result.err.status;
    if (result.status == callback_status::ok) {
        complete.status = command_status::ok;
    } else if (result.status == callback_status::stopped) {
        complete.status = command_status::stopped;
    }
    state->record_trace(complete);
}

inline auto enqueue_callback(
    queue& q,
    host_callback_dispatcher& dispatcher,
    callback_id callback) {
    auto queue_state = q.queue_.lock();
    return __detail::__make_command_sender(
        queue_state,
        [queue_state, &dispatcher, callback] {
            auto result = dispatcher.invoke(callback);
            __record_callback_trace(queue_state, result);
            if (result.status == callback_status::stopped) {
                throw __detail::__stopped_signal{};
            }
            if (!result) {
                throw operation_error{
                    result.err.kind,
                    result.err.status,
                    "forge::accel::mock::host callback failed"};
            }
        });
}

inline auto enqueue_callback_typed(
    queue& q,
    host_callback_dispatcher& dispatcher,
    callback_id callback) {
    return __typed_detail::void_sender(enqueue_callback(q, dispatcher, callback));
}

} // namespace forge::accel::mock
