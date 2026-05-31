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

#if !defined(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND)
#error "forge::io windows context requires FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../error.hpp"
#include "../../resource_policy.hpp"

#include <execution>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <span>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace forge::io {

struct context_options {
    std::pmr::memory_resource* memory = forge::default_memory_resource();
    std::size_t max_events = 64;
};

class context;

namespace __detail {

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

[[nodiscard]] inline auto __windows_error(DWORD code, const char* what)
    -> std::exception_ptr {
    return std::make_exception_ptr(std::system_error{
        static_cast<int>(code), std::system_category(), what});
}

[[nodiscard]] inline auto __windows_error(const char* what)
    -> std::exception_ptr {
    return __windows_error(::GetLastError(), what);
}

class __handle {
public:
    __handle() noexcept = default;
    explicit __handle(HANDLE handle) noexcept : handle_(handle) {}

    ~__handle() noexcept {
        reset();
    }

    __handle(__handle&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}

    auto operator=(__handle&& other) noexcept -> __handle& {
        if (this != &other) {
            reset(std::exchange(other.handle_, nullptr));
        }
        return *this;
    }

    __handle(const __handle&) = delete;
    auto operator=(const __handle&) -> __handle& = delete;

    [[nodiscard]] auto get() const noexcept -> HANDLE { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ && handle_ != INVALID_HANDLE_VALUE;
    }

    void reset(HANDLE next = nullptr) noexcept {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
        handle_ = next;
    }

private:
    HANDLE handle_ = nullptr;
};

struct __record_base;

struct __overlapped_entry {
    OVERLAPPED overlapped{};
    __record_base* record = nullptr;
};

enum class __operation_kind {
    read,
    write
};

struct __record_base {
    virtual ~__record_base() = default;
    virtual void complete_value(std::size_t bytes) noexcept = 0;
    virtual void complete_error(std::exception_ptr error) noexcept = 0;
    virtual void complete_stopped() noexcept = 0;

    __overlapped_entry entry;
    HANDLE handle = INVALID_HANDLE_VALUE;
    __operation_kind kind = __operation_kind::read;
    std::span<std::byte> read_buffer;
    std::span<const std::byte> write_buffer;
    std::atomic<bool> done{false};
    bool cancel_requested = false;
};

template<class R>
struct __record final : __record_base {
    __record(
        HANDLE h,
        __operation_kind op,
        std::span<std::byte> read,
        std::span<const std::byte> write,
        R r)
        : rcvr(std::move(r)) {
        handle = h;
        kind = op;
        read_buffer = read;
        write_buffer = write;
        entry.record = this;
    }

    void complete_value(std::size_t bytes) noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        std::execution::set_value(std::move(rcvr), bytes);
    }

    void complete_error(std::exception_ptr error) noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        std::execution::set_error(std::move(rcvr), std::move(error));
    }

    void complete_stopped() noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        std::execution::set_stopped(std::move(rcvr));
    }

    R rcvr;
};

using __record_ptr = std::shared_ptr<__record_base>;

struct __handle_hash {
    [[nodiscard]] auto operator()(HANDLE handle) const noexcept -> std::size_t {
        return std::hash<void*>{}(handle);
    }
};

enum class __start_result_kind {
    accepted,
    stopped,
    error
};

struct __start_result {
    __start_result_kind kind = __start_result_kind::accepted;
    std::exception_ptr error;
};

struct __state : std::enable_shared_from_this<__state> {
    explicit __state(context_options options)
        : memory(forge::normalize_memory_resource(options.memory))
        , pending_records(
              0,
              std::hash<__record_base*>{},
              std::equal_to<__record_base*>{},
              std::pmr::polymorphic_allocator<
                  std::pair<__record_base* const, __record_ptr>>{memory})
        , associated_handles(
              0,
              __handle_hash{},
              std::equal_to<HANDLE>{},
              std::pmr::polymorphic_allocator<HANDLE>{memory}) {
        (void)options.max_events;
        port.reset(::CreateIoCompletionPort(
            INVALID_HANDLE_VALUE, nullptr, 0, 1));
        if (!port) {
            throw std::system_error{
                static_cast<int>(::GetLastError()),
                std::system_category(),
                "CreateIoCompletionPort"};
        }
    }

    [[nodiscard]] auto start(__record_ptr record) -> __start_result {
        __start_result result{};
        {
            std::lock_guard lk{mtx};
            if (closed || stopped) {
                result.kind = __start_result_kind::stopped;
                return result;
            }
            if (!record->handle || record->handle == INVALID_HANDLE_VALUE) {
                result.kind = __start_result_kind::error;
                result.error = __windows_error(
                    ERROR_INVALID_HANDLE,
                    "forge::io IOCP invalid handle");
                return result;
            }

            if (!associated_handles.contains(record->handle)) {
                HANDLE associated = ::CreateIoCompletionPort(
                    record->handle, port.get(), 0, 0);
                if (!associated) {
                    result.kind = __start_result_kind::error;
                    result.error = __windows_error(
                        "CreateIoCompletionPort associate handle");
                    return result;
                }
                associated_handles.insert(record->handle);
            }

            auto* key = record.get();
            pending_records.emplace(key, record);
            ++pending;

            if (!issue_locked(*record)) {
                auto error = ::GetLastError();
                if (error == ERROR_IO_PENDING) {
                    return result;
                }

                pending_records.erase(key);
                --pending;
                if (pending == 0) {
                    cv.notify_all();
                }

                result.kind = __start_result_kind::error;
                result.error = __windows_error(error, "forge::io IOCP issue");
                return result;
            }
        }
        return result;
    }

    void cancel(HANDLE handle) noexcept {
        {
            std::lock_guard lk{mtx};
            for (auto& [_, record] : pending_records) {
                if (record->handle == handle) {
                    record->cancel_requested = true;
                    ::CancelIoEx(handle, &record->entry.overlapped);
                }
            }
        }
        wake_worker();
    }

    void close() noexcept {
        {
            std::lock_guard lk{mtx};
            closed = true;
        }
        wake_worker();
    }

    void request_stop() noexcept {
        {
            std::lock_guard lk{mtx};
            stopped = true;
            for (auto& [_, record] : pending_records) {
                record->cancel_requested = true;
                ::CancelIoEx(record->handle, &record->entry.overlapped);
            }
        }
        wake_worker();
    }

    void shutdown() noexcept {
        close();
        request_stop();
    }

    void run() noexcept {
        {
            std::lock_guard lk{mtx};
            worker_id = std::this_thread::get_id();
        }

        while (true) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* overlapped = nullptr;
            BOOL ok = ::GetQueuedCompletionStatus(
                port.get(), &bytes, &key, &overlapped, INFINITE);

            if (!overlapped) {
                if (should_exit()) {
                    break;
                }
                continue;
            }

            auto* entry = reinterpret_cast<__overlapped_entry*>(
                reinterpret_cast<char*>(overlapped) -
                offsetof(__overlapped_entry, overlapped));
            complete(entry->record, ok != FALSE, bytes, ::GetLastError());

            if (should_exit()) {
                break;
            }
        }

        {
            std::lock_guard lk{mtx};
            worker_done = true;
            cv.notify_all();
        }
    }

    [[nodiscard]] auto called_from_worker() noexcept -> bool {
        std::lock_guard lk{mtx};
        return worker_id == std::this_thread::get_id();
    }

    void wake_worker() noexcept {
        if (port) {
            ::PostQueuedCompletionStatus(port.get(), 0, 0, nullptr);
        }
    }

    [[nodiscard]] auto memory_resource() const noexcept -> std::pmr::memory_resource* {
        return memory;
    }

private:
    [[nodiscard]] bool issue_locked(__record_base& record) noexcept {
        record.entry.overlapped = OVERLAPPED{};
        record.entry.record = &record;

        const auto byte_count = record.kind == __operation_kind::read
            ? record.read_buffer.size()
            : record.write_buffer.size();
        if (byte_count > std::numeric_limits<DWORD>::max()) {
            ::SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }

        const auto bytes = static_cast<DWORD>(byte_count);
        if (record.kind == __operation_kind::read) {
            return ::ReadFile(
                record.handle,
                record.read_buffer.data(),
                bytes,
                nullptr,
                &record.entry.overlapped) != FALSE;
        }
        return ::WriteFile(
            record.handle,
            record.write_buffer.data(),
            bytes,
            nullptr,
            &record.entry.overlapped) != FALSE;
    }

    void complete(
        __record_base* raw_record,
        bool ok,
        DWORD bytes,
        DWORD error) noexcept {
        __record_ptr record;
        {
            std::lock_guard lk{mtx};
            auto it = pending_records.find(raw_record);
            if (it == pending_records.end()) {
                return;
            }
            record = std::move(it->second);
            pending_records.erase(it);
            if (pending > 0) {
                --pending;
            }
            if (pending == 0) {
                cv.notify_all();
            }
        }

        if (record->cancel_requested || error == ERROR_OPERATION_ABORTED) {
            record->complete_stopped();
        } else if (ok) {
            record->complete_value(static_cast<std::size_t>(bytes));
        } else {
            record->complete_error(__windows_error(error, "forge::io IOCP completion"));
        }
    }

    [[nodiscard]] bool should_exit() noexcept {
        std::lock_guard lk{mtx};
        return (closed || stopped) && pending == 0;
    }

public:
    std::pmr::memory_resource* memory;
    __handle port;
    std::mutex mtx;
    std::condition_variable cv;
    std::pmr::unordered_map<__record_base*, __record_ptr> pending_records;
    std::pmr::unordered_set<HANDLE, __handle_hash> associated_handles;
    bool closed = false;
    bool stopped = false;
    bool worker_done = false;
    std::size_t pending = 0;
    std::thread::id worker_id{};
};

template<class R>
struct __op {
    using operation_state_concept = std::execution::operation_state_t;
    using record_t = __record<R>;

    __op(
        std::shared_ptr<__state> state,
        HANDLE handle,
        __operation_kind kind,
        std::span<std::byte> read_buffer,
        std::span<const std::byte> write_buffer,
        R rcvr)
        : state_(std::move(state))
        , record_(std::allocate_shared<record_t>(
              std::pmr::polymorphic_allocator<record_t>{
                  state_->memory_resource()},
              handle,
              kind,
              read_buffer,
              write_buffer,
              std::move(rcvr))) {}

    __op(__op&&) = delete;
    auto operator=(__op&&) -> __op& = delete;
    __op(const __op&) = delete;
    auto operator=(const __op&) -> __op& = delete;

    void start() & noexcept {
        try {
            if (!state_) {
                record_->complete_stopped();
                return;
            }
            if (__stop_requested(record_->rcvr)) {
                record_->complete_stopped();
                return;
            }

            auto result = state_->start(record_);
            switch (result.kind) {
            case __start_result_kind::accepted:
                break;
            case __start_result_kind::stopped:
                record_->complete_stopped();
                break;
            case __start_result_kind::error:
                record_->complete_error(std::move(result.error));
                break;
            }
        } catch (...) {
            record_->complete_error(std::current_exception());
        }
    }

    std::shared_ptr<__state> state_;
    std::shared_ptr<record_t> record_;
};

struct __byte_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__state> state;
    HANDLE handle = INVALID_HANDLE_VALUE;
    __operation_kind kind = __operation_kind::read;
    std::span<std::byte> read_buffer;
    std::span<const std::byte> write_buffer;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(std::size_t),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{
            std::move(state),
            handle,
            kind,
            read_buffer,
            write_buffer,
            std::move(rcvr)};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{
            state,
            handle,
            kind,
            read_buffer,
            write_buffer,
            std::move(rcvr)};
    }
};

} // namespace __detail

class context {
public:
    explicit context(context_options options = {})
        : state_(__make_state(options))
        , thread_([state = state_] { state->run(); }) {}

    ~context() noexcept {
        shutdown();
        wait();
    }

    context(const context&) = delete;
    auto operator=(const context&) -> context& = delete;
    context(context&&) = delete;
    auto operator=(context&&) -> context& = delete;

    [[nodiscard]] auto async_read_some(HANDLE handle, std::span<std::byte> buffer)
        -> __detail::__byte_sender {
        return __detail::__byte_sender{
            state_,
            handle,
            __detail::__operation_kind::read,
            buffer,
            {}};
    }

    [[nodiscard]] auto async_read_some_typed(
        HANDLE handle,
        std::span<std::byte> buffer) {
        return __typed_detail::size_sender(async_read_some(handle, buffer));
    }

    [[nodiscard]] auto async_write_some(
        HANDLE handle,
        std::span<const std::byte> buffer) -> __detail::__byte_sender {
        return __detail::__byte_sender{
            state_,
            handle,
            __detail::__operation_kind::write,
            {},
            buffer};
    }

    [[nodiscard]] auto async_write_some_typed(
        HANDLE handle,
        std::span<const std::byte> buffer) {
        return __typed_detail::size_sender(async_write_some(handle, buffer));
    }

    void cancel(HANDLE handle) noexcept {
        state_->cancel(handle);
    }

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
        if (!thread_.joinable()) {
            return;
        }
        if (state_->called_from_worker()) {
            thread_.detach();
            return;
        }
        thread_.join();
    }

private:
    static auto __make_state(context_options options)
        -> std::shared_ptr<__detail::__state> {
        options.memory = forge::normalize_memory_resource(options.memory);
        return std::allocate_shared<__detail::__state>(
            std::pmr::polymorphic_allocator<__detail::__state>{options.memory},
            options);
    }

    std::shared_ptr<__detail::__state> state_;
    std::thread thread_;
};

} // namespace forge::io
