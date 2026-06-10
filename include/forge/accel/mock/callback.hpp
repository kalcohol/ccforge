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

#include <algorithm>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

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

struct host_callback_dispatcher_options {
    std::pmr::memory_resource* memory = forge::default_memory_resource();
    std::optional<std::size_t> completion_capacity = 1024;
};

class host_callback_dispatcher {
public:
    explicit host_callback_dispatcher(
        std::pmr::memory_resource* memory = forge::default_memory_resource())
        : host_callback_dispatcher(host_callback_dispatcher_options{
              .memory = memory})
    {}

    explicit host_callback_dispatcher(
        host_callback_dispatcher_options options)
        : state_(std::make_shared<state>(options))
    {}

    ~host_callback_dispatcher() noexcept {
        state_->retire_owner();
    }

    host_callback_dispatcher(const host_callback_dispatcher&) = delete;
    auto operator=(const host_callback_dispatcher&)
        -> host_callback_dispatcher& = delete;
    host_callback_dispatcher(host_callback_dispatcher&&) = delete;
    auto operator=(host_callback_dispatcher&&)
        -> host_callback_dispatcher& = delete;

    template<class Handler>
    [[nodiscard]] auto register_callback(Handler&& handler) -> callback_id {
        auto wrapped = wrap_handler(static_cast<Handler&&>(handler));
        return state_->register_auto(std::move(wrapped));
    }

    template<class Handler>
    void register_callback(callback_id id, Handler&& handler) {
        state_->register_explicit(
            id,
            wrap_handler(static_cast<Handler&&>(handler)));
    }

    void unregister_callback(callback_id id) noexcept {
        state_->unregister_callback(id);
    }

    void close() noexcept {
        state_->close();
    }

    void request_stop() noexcept {
        close();
    }

    void shutdown() noexcept {
        close();
        request_stop();
    }

    void wait() noexcept {
        state_->wait();
    }

    [[nodiscard]] auto invoke(callback_id id) -> callback_result {
        auto token = capture(id);
        return token.owner->invoke(std::move(token));
    }

    [[nodiscard]] auto completions() const -> std::vector<callback_result> {
        return state_->completion_snapshot();
    }

#ifdef FORGE_ENABLE_TEST_HOOKS
    [[nodiscard]] auto test_record_count() const noexcept -> std::size_t {
        return state_->record_count();
    }
#endif

private:
    using callback_fn = std::function<void(callback_invoke_id)>;

    struct state;

    struct callback_token {
        std::shared_ptr<state> owner;
        callback_id id{};
        std::uint64_t epoch = 0;
        bool known = false;
        bool stopped = false;
    };

    struct stack_token {
        const void* owner = nullptr;
        std::uint64_t id = 0;
        std::uint64_t epoch = 0;

        friend auto operator==(stack_token, stack_token) -> bool = default;
    };

    [[nodiscard]] static auto callback_stack() -> std::vector<stack_token>& {
        thread_local std::vector<stack_token> stack;
        return stack;
    }

    [[nodiscard]] static bool is_running_on_this_thread(stack_token token) {
        auto& stack = callback_stack();
        return std::find(stack.begin(), stack.end(), token) != stack.end();
    }

    // True when the calling thread is currently executing any callback body
    // owned by `owner` (a `state*`). Used so wait()/retire cannot block on an
    // invocation that the calling thread itself is running (self-deadlock).
    [[nodiscard]] static bool is_owner_running_on_this_thread(
        const void* owner) {
        auto& stack = callback_stack();
        return std::any_of(
            stack.begin(),
            stack.end(),
            [owner](stack_token token) { return token.owner == owner; });
    }

    struct callback_stack_guard {
        explicit callback_stack_guard(stack_token token_arg)
            : token(token_arg) {
            callback_stack().push_back(token);
        }

        ~callback_stack_guard() {
            auto& stack = callback_stack();
            if (!stack.empty() && stack.back() == token) {
                stack.pop_back();
                return;
            }
            auto it = std::find(stack.begin(), stack.end(), token);
            if (it != stack.end()) {
                stack.erase(it);
            }
        }

        stack_token token;
    };

    struct record {
        record(callback_id id_arg, std::uint64_t epoch_arg, callback_fn fn_arg)
            : id(id_arg)
            , epoch(epoch_arg)
            , fn(std::make_shared<callback_fn>(std::move(fn_arg)))
        {}

        callback_id id{};
        std::uint64_t epoch = 0;
        std::shared_ptr<callback_fn> fn;
        std::size_t in_flight = 0;
        bool registered = true;
    };

    struct state {
        using record_map = std::pmr::unordered_map<
            std::uint64_t,
            std::shared_ptr<record>>;

        explicit state(host_callback_dispatcher_options options)
            : memory(normalize_memory_resource(options.memory))
            , completion_capacity(options.completion_capacity)
            , records(typename record_map::allocator_type{memory})
            , completions_(
                  std::in_place,
                  std::pmr::polymorphic_allocator<callback_result>{memory})
        {}

        [[nodiscard]] auto register_auto(callback_fn fn) -> callback_id {
            std::lock_guard lk{mtx};
            throw_if_closed();
            auto id = next_available_id_locked();
            emplace_record_locked(id, std::move(fn));
            return id;
        }

        void register_explicit(callback_id id, callback_fn fn) {
            std::lock_guard lk{mtx};
            throw_if_closed();
            auto it = records->find(id.value);
            if (it != records->end() && it->second && it->second->registered) {
                throw operation_error{
                    error_kind::protocol_error,
                    "forge::accel::mock::host callback id is already registered"};
            }
            emplace_record_locked(id, std::move(fn));
        }

        void unregister_callback(callback_id id) noexcept {
            std::shared_ptr<callback_fn> released_fn;
            std::unique_lock lk{mtx};
            if (!records) {
                return;
            }
            auto it = records->find(id.value);
            if (it == records->end() || !it->second) {
                return;
            }
            auto rec = it->second;
            rec->registered = false;
            released_fn = std::move(rec->fn);
            if (is_running_on_this_thread(token_for(*rec))) {
                prune_if_unused_locked(rec);
                cv.notify_all();
                return;
            }
            ++active_unregisters;
            auto finish_unregister = [&] {
                if (active_unregisters > 0) {
                    --active_unregisters;
                }
                cv.notify_all();
            };
            try {
                cv.wait(lk, [&] { return rec->in_flight == 0; });
            } catch (...) {
                finish_unregister();
                return;
            }
            finish_unregister();
            prune_if_unused_locked(rec);
        }

        void close() noexcept {
            {
                std::lock_guard lk{mtx};
                close_locked();
            }
            cv.notify_all();
        }

        void wait() noexcept {
            std::unique_lock lk{mtx};
            // A callback body that waits on its own dispatcher cannot drain
            // itself; return instead of self-deadlocking (mirrors unregister).
            if (is_owner_running_on_this_thread(this)) {
                return;
            }
            try {
                cv.wait(lk, [&] { return all_drained_locked(); });
            } catch (...) {
                return;
            }
            prune_all_locked();
        }

        void retire_owner() noexcept {
            std::unique_lock lk{mtx};
            close_locked();
            // If the owner is destroyed from inside one of its own in-flight
            // callbacks, skip the drain wait to avoid self-deadlock; that
            // in-flight invoke keeps `state` alive via its captured token.
            if (!is_owner_running_on_this_thread(this)) {
                try {
                    cv.wait(lk, [&] { return all_drained_locked(); });
                } catch (...) {
                }
            }
            retired = true;
            auto old_records = take_records_locked();
            completions_.reset();
            lk.unlock();
        }

        [[nodiscard]] auto capture(callback_id id) noexcept -> callback_token {
            std::lock_guard lk{mtx};
            if (closed) {
                return callback_token{
                    .id = id,
                    .stopped = true};
            }
            if (!records) {
                return callback_token{
                    .id = id,
                    .known = false};
            }
            auto it = records->find(id.value);
            if (it == records->end() || !it->second || !it->second->registered) {
                return callback_token{
                    .id = id,
                    .known = false};
            }
            return callback_token{
                .id = id,
                .epoch = it->second->epoch,
                .known = true};
        }

        [[nodiscard]] auto invoke(callback_token token) -> callback_result {
            auto result = callback_result{
                .callback = token.id,
                .invoke = callback_invoke_id{
                    next_invoke.fetch_add(1, std::memory_order_relaxed)},
                .started = std::chrono::steady_clock::now(),
            };

            std::shared_ptr<record> rec;
            std::shared_ptr<callback_fn> fn;
            {
                std::unique_lock lk{mtx};
                if (token.stopped || closed) {
                    result.status = callback_status::stopped;
                    result.err = error{
                        error_kind::aborted,
                        command_status::stopped};
                    result.completed = std::chrono::steady_clock::now();
                    record_completion_locked(result);
                    return result;
                }
                if (!token.known) {
                    result.status = callback_status::missing;
                    result.err = error{error_kind::protocol_error};
                    result.completed = std::chrono::steady_clock::now();
                    record_completion_locked(result);
                    return result;
                }
                if (!records) {
                    result.status = callback_status::stopped;
                    result.err = error{
                        error_kind::aborted,
                        command_status::stopped};
                    result.completed = std::chrono::steady_clock::now();
                    record_completion_locked(result);
                    return result;
                }
                auto it = records->find(token.id.value);
                if (it == records->end() || !it->second ||
                    !it->second->registered || it->second->epoch != token.epoch) {
                    result.status = callback_status::stopped;
                    result.err = error{
                        error_kind::aborted,
                        command_status::stopped};
                    result.completed = std::chrono::steady_clock::now();
                    record_completion_locked(result);
                    return result;
                }
                rec = it->second;
                fn = rec->fn;
                if (!fn) {
                    result.status = callback_status::stopped;
                    result.err = error{
                        error_kind::aborted,
                        command_status::stopped};
                    result.completed = std::chrono::steady_clock::now();
                    record_completion_locked(result);
                    return result;
                }
                ++rec->in_flight;
                ++active_invocations;
            }

            try {
                callback_stack_guard guard{token_for(*rec)};
                (*fn)(result.invoke);
            } catch (...) {
                result.status = callback_status::failed;
                result.err = __typed_detail::from_exception(std::current_exception());
            }

            result.completed = std::chrono::steady_clock::now();
            record_completion(result);
            release_record(rec, fn);
            return result;
        }

        [[nodiscard]] auto completion_snapshot() const
            -> std::vector<callback_result> {
            std::lock_guard lk{mtx};
            if (!completions_) {
                return {};
            }
            return {completions_->begin(), completions_->end()};
        }

        [[nodiscard]] auto record_count() const noexcept -> std::size_t {
            std::lock_guard lk{mtx};
            return records ? records->size() : 0;
        }

        void record_completion(callback_result result) noexcept {
            try {
                std::lock_guard lk{mtx};
                record_completion_locked(std::move(result));
            } catch (...) {
            }
        }

        std::pmr::memory_resource* memory;
        std::optional<std::size_t> completion_capacity;
        std::atomic<std::uint64_t> next_invoke{1};
        mutable std::mutex mtx;
        std::condition_variable cv;
        bool closed = false;
        bool retired = false;
        std::size_t active_invocations = 0;
        std::size_t active_unregisters = 0;
        std::uint64_t next_callback = 1;
        std::uint64_t next_epoch = 1;
        std::optional<record_map> records;
        std::optional<std::pmr::deque<callback_result>> completions_;

    private:
        [[nodiscard]] auto token_for(const record& rec) const noexcept
            -> stack_token {
            return stack_token{this, rec.id.value, rec.epoch};
        }

        [[nodiscard]] auto all_drained_locked() const noexcept -> bool {
            if (records) {
                for (const auto& item : *records) {
                    if (item.second && item.second->in_flight != 0) {
                        return false;
                    }
                }
            }
            return active_invocations == 0 && active_unregisters == 0;
        }

        void throw_if_closed() const {
            if (closed) {
                throw operation_error{
                    error_kind::invalid_context,
                    "forge::accel::mock::host_callback_dispatcher is closed"};
            }
        }

        [[nodiscard]] auto next_available_id_locked() -> callback_id {
            auto first = next_callback;
            for (;;) {
                auto value = next_callback++;
                if (next_callback == 0) {
                    next_callback = 1;
                }
                if (value != 0 && records->find(value) == records->end()) {
                    return callback_id{value};
                }
                if (next_callback == first) {
                    throw operation_error{
                        error_kind::resource_exhausted,
                        "forge::accel::mock::host callback id space is exhausted"};
                }
            }
        }

        void emplace_record_locked(callback_id id, callback_fn fn) {
            auto epoch = next_epoch++;
            if (next_epoch == 0) {
                next_epoch = 1;
            }
            auto rec = std::make_shared<record>(id, epoch, std::move(fn));
            records->insert_or_assign(id.value, std::move(rec));
        }

        void close_locked() noexcept {
            closed = true;
        }

        void prune_if_unused_locked(const std::shared_ptr<record>& rec) {
            if (!rec || rec->registered || rec->in_flight != 0) {
                return;
            }
            if (!records) {
                return;
            }
            auto it = records->find(rec->id.value);
            if (it != records->end() && it->second == rec) {
                records->erase(it);
            }
        }

        void prune_all_locked() {
            if (!records) {
                return;
            }
            for (auto it = records->begin(); it != records->end();) {
                if (it->second && !it->second->registered &&
                    it->second->in_flight == 0) {
                    it = records->erase(it);
                } else {
                    ++it;
                }
            }
        }

        void release_record(
            std::shared_ptr<record>& rec,
            std::shared_ptr<callback_fn>& fn) noexcept {
            {
                std::lock_guard lk{mtx};
                if (rec->in_flight > 0) {
                    --rec->in_flight;
                }
                prune_if_unused_locked(rec);
            }
            fn.reset();
            rec.reset();
            {
                std::lock_guard lk{mtx};
                if (active_invocations > 0) {
                    --active_invocations;
                }
            }
            cv.notify_all();
        }

        void record_completion_locked(callback_result result) noexcept {
            try {
                if (retired || !completions_) {
                    return;
                }
                if (!completion_capacity) {
                    completions_->push_back(std::move(result));
                    return;
                }
                if (*completion_capacity == 0) {
                    return;
                }
                if (completions_->size() == *completion_capacity) {
                    completions_->pop_front();
                }
                completions_->push_back(std::move(result));
            } catch (...) {
            }
        }

        [[nodiscard]] auto take_records_locked() noexcept -> record_map {
            record_map old_records{typename record_map::allocator_type{memory}};
            if (records) {
                records->swap(old_records);
                records.reset();
            }
            return old_records;
        }

    };

    template<class Handler>
    [[nodiscard]] static auto wrap_handler(Handler&& handler) -> callback_fn {
        return callback_fn{
            [handler = std::forward<Handler>(handler)](
                callback_invoke_id invoke) mutable {
                if constexpr (std::is_invocable_v<Handler&, callback_invoke_id>) {
                    std::invoke(handler, invoke);
                } else {
                    std::invoke(handler);
                }
            }};
    }

    [[nodiscard]] auto capture(callback_id id) const noexcept -> callback_token {
        auto token = state_->capture(id);
        token.owner = state_;
        return token;
    }

    friend auto enqueue_callback(
        queue&,
        host_callback_dispatcher&,
        callback_id);

    std::shared_ptr<state> state_;
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
    }
    state->record_trace(complete);
}

inline auto enqueue_callback(
    queue& q,
    host_callback_dispatcher& dispatcher,
    callback_id callback) {
    auto queue_state = q.queue_.lock();
    auto token = dispatcher.capture(callback);
    return __detail::__make_command_sender(
        queue_state,
        [queue_state, token = std::move(token)] {
            auto result = token.owner->invoke(token);
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
