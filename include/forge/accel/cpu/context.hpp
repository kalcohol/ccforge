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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <execution>
#include <functional>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace forge::accel::cpu {

struct context_options {
    std::size_t thread_count = 2;
    std::optional<std::size_t> queue_capacity = std::nullopt;
    std::size_t device_count = 1;
    std::pmr::memory_resource* memory = forge::default_memory_resource();
};

struct event_wait_options {
    std::optional<std::chrono::steady_clock::duration> timeout = std::nullopt;
};

struct event_snapshot {
    event_generation record_generation{};
    event_generation completed_generation{};
    bool ready = false;
};

class context;
class queue;
class device;
class event;
template<class T>
class host_buffer;
template<class T>
class device_buffer;

#include "detail/state.hpp"

class event {
public:
    event()
        : state_(std::make_shared<__detail::__event_state>())
    {}

    [[nodiscard]] bool ready() const noexcept {
        return state_ && state_->snapshot().ready;
    }

    [[nodiscard]] auto query() const noexcept -> event_snapshot {
        return state_ ? state_->snapshot() : event_snapshot{};
    }

private:
    friend auto record_event(queue&, event&);
    friend auto record_event_typed(queue&, event&);
    friend auto wait_event(queue&, event&, event_wait_options);
    friend auto wait_event_typed(queue&, event&, event_wait_options);
    friend auto synchronize_event(queue&, event&, event_wait_options);
    friend auto synchronize_event_typed(queue&, event&, event_wait_options);
    friend auto query_event(event&);
    friend auto query_event_typed(event&);

    std::shared_ptr<__detail::__event_state> state_;
};

class queue {
public:
    queue() = default;

    [[nodiscard]] auto kind() const noexcept -> queue_kind {
        return state_ ? state_->kind : queue_kind::general;
    }

    [[nodiscard]] bool closed() const noexcept {
        auto state = state_ ? state_->owner.lock() : nullptr;
        return !state || state->closed_now();
    }

private:
    explicit queue(std::shared_ptr<__detail::__queue_state> state)
        : state_(std::move(state))
    {}

    friend class context;
    friend class device;
    template<class T>
    friend auto copy_to_device(queue&, device_buffer<T>&, std::span<const T>);
    template<class T>
    friend auto copy_to_host(queue&, std::span<T>, const device_buffer<T>&);
    template<class T>
    friend auto copy_device_to_device(queue&, device_buffer<T>&, const device_buffer<T>&);
    template<class F>
    friend auto submit(queue&, F&&);
    friend auto record_event(queue&, event&);
    friend auto wait_event(queue&, event&, event_wait_options);
    friend auto synchronize_event(queue&, event&, event_wait_options);
    friend auto fence(queue&);

    std::shared_ptr<__detail::__queue_state> state_;
};

class device {
public:
    device() = default;

    [[nodiscard]] auto info() const noexcept -> device_info {
        return state_ ? state_->info : device_info{};
    }

    [[nodiscard]] auto get_queue(queue_kind kind = queue_kind::general) const -> queue {
        auto owner = owner_.lock();
        if (!owner || !state_) {
            throw operation_error{
                error_kind::invalid_context,
                "forge::accel::cpu: invalid device"};
        }
        return queue{owner->make_queue(kind, state_)};
    }

private:
    device(
        std::shared_ptr<__detail::__state> owner,
        std::shared_ptr<__detail::__device_state> state)
        : owner_(std::move(owner))
        , state_(std::move(state))
    {}

    friend class context;

    std::weak_ptr<__detail::__state> owner_;
    std::shared_ptr<__detail::__device_state> state_;
};

class context {
public:
    explicit context(context_options options = {})
        : state_(std::allocate_shared<__detail::__state>(
              std::pmr::polymorphic_allocator<__detail::__state>{
                  normalize_memory_resource(options.memory)},
              options))
    {}

    ~context() noexcept {
        shutdown();
        wait();
    }

    context(const context&) = delete;
    context& operator=(const context&) = delete;
    context(context&&) = delete;
    context& operator=(context&&) = delete;

    [[nodiscard]] auto get_queue(queue_kind kind = queue_kind::general) -> queue {
        return queue{state_->make_queue(kind)};
    }

    [[nodiscard]] auto get_device(device_id id = device_id{0}) -> device {
        return device{state_, state_->get_device(id)};
    }

    [[nodiscard]] auto devices() -> std::vector<device> {
        std::vector<device> out;
        out.reserve(state_->devices.size());
        for (auto& dev : state_->devices) {
            out.emplace_back(device{state_, dev});
        }
        return out;
    }

    [[nodiscard]] auto device_infos() const -> std::vector<device_info> {
        std::vector<device_info> out;
        out.reserve(state_->devices.size());
        for (auto& dev : state_->devices) {
            out.push_back(dev->info);
        }
        return out;
    }

    [[nodiscard]] auto memory_resource() const noexcept -> std::pmr::memory_resource* {
        return state_->memory;
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
        state_->wait();
    }

private:
    template<class T>
    friend class host_buffer;
    template<class T>
    friend class device_buffer;

    std::shared_ptr<__detail::__state> state_;
};

template<class T>
class host_buffer {
    static_assert(std::is_trivially_copyable_v<T>);

public:
    host_buffer(
        context& ctx,
        std::size_t size,
        memory_kind kind = memory_kind::host)
        : memory_(ctx.memory_resource())
        , kind_(kind)
        , data_(std::pmr::polymorphic_allocator<T>{memory_})
    {
        if (kind != memory_kind::host && kind != memory_kind::pinned_host &&
                kind != memory_kind::mapped_host && kind != memory_kind::managed) {
            throw operation_error{
                error_kind::invalid_memory_kind,
                "forge::accel::cpu: invalid host memory kind"};
        }
        data_.resize(size);
    }

    [[nodiscard]] auto span() noexcept -> std::span<T> {
        return std::span<T>{data_.data(), data_.size()};
    }

    [[nodiscard]] auto span() const noexcept -> std::span<const T> {
        return std::span<const T>{data_.data(), data_.size()};
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return data_.size();
    }

    [[nodiscard]] auto kind() const noexcept -> memory_kind {
        return kind_;
    }

private:
    std::pmr::memory_resource* memory_;
    memory_kind kind_;
    std::pmr::vector<T> data_;
};

template<class T>
class device_buffer {
    static_assert(std::is_trivially_copyable_v<T>);

public:
    static constexpr std::size_t alignment = 64;

    device_buffer(
        context& ctx,
        std::size_t size,
        memory_kind kind = memory_kind::device)
        : memory_(ctx.memory_resource())
        , kind_(kind)
        , size_(size)
    {
        if (kind != memory_kind::device && kind != memory_kind::cached_device &&
                kind != memory_kind::managed) {
            throw operation_error{
                error_kind::invalid_memory_kind,
                "forge::accel::cpu: invalid device memory kind"};
        }
        allocate();
    }

    ~device_buffer() noexcept {
        deallocate();
    }

    device_buffer(const device_buffer&) = delete;
    device_buffer& operator=(const device_buffer&) = delete;

    device_buffer(device_buffer&& other) noexcept
        : memory_(std::exchange(other.memory_, forge::default_memory_resource()))
        , kind_(other.kind_)
        , size_(std::exchange(other.size_, 0))
        , data_(std::exchange(other.data_, nullptr))
    {}

    device_buffer& operator=(device_buffer&& other) noexcept {
        if (this != &other) {
            deallocate();
            memory_ = std::exchange(other.memory_, forge::default_memory_resource());
            kind_ = other.kind_;
            size_ = std::exchange(other.size_, 0);
            data_ = std::exchange(other.data_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] auto span() noexcept -> std::span<T> {
        return std::span<T>{data_, size_};
    }

    [[nodiscard]] auto span() const noexcept -> std::span<const T> {
        return std::span<const T>{data_, size_};
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return size_;
    }

    [[nodiscard]] auto kind() const noexcept -> memory_kind {
        return kind_;
    }

private:
    void allocate() {
        if (size_ == 0) {
            return;
        }
        if (size_ > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw operation_error{
                error_kind::invalid_buffer,
                "forge::accel::cpu: device buffer size overflows allocation"};
        }
        constexpr std::size_t align = alignment < alignof(T) ? alignof(T) : alignment;
        const auto bytes = sizeof(T) * size_;
        data_ = static_cast<T*>(memory_->allocate(bytes, align));
        std::size_t constructed = 0;
        try {
            for (; constructed < size_; ++constructed) {
                std::construct_at(data_ + constructed);
            }
        } catch (...) {
            while (constructed > 0) {
                --constructed;
                std::destroy_at(data_ + constructed);
            }
            memory_->deallocate(data_, bytes, align);
            data_ = nullptr;
            throw;
        }
    }

    void deallocate() noexcept {
        if (!data_) {
            return;
        }
        for (std::size_t i = 0; i < size_; ++i) {
            std::destroy_at(data_ + i);
        }
        constexpr std::size_t align = alignment < alignof(T) ? alignof(T) : alignment;
        memory_->deallocate(data_, sizeof(T) * size_, align);
        data_ = nullptr;
        size_ = 0;
    }

    std::pmr::memory_resource* memory_;
    memory_kind kind_;
    std::size_t size_ = 0;
    T* data_ = nullptr;
};

template<class T>
[[nodiscard]] auto copy_to_device(
    queue& q,
    device_buffer<T>& dst,
    std::span<const T> src) {
    return __detail::make_command_sender(q.state_, [&dst, src] {
        if (dst.size() != src.size()) {
            throw operation_error{
                error_kind::size_mismatch,
                "forge::accel::cpu: H2D size mismatch"};
        }
        std::copy(src.begin(), src.end(), dst.span().begin());
    });
}

template<class T>
[[nodiscard]] auto copy_to_host(
    queue& q,
    std::span<T> dst,
    const device_buffer<T>& src) {
    return __detail::make_command_sender(q.state_, [dst, &src] {
        if (dst.size() != src.size()) {
            throw operation_error{
                error_kind::size_mismatch,
                "forge::accel::cpu: D2H size mismatch"};
        }
        std::copy(src.span().begin(), src.span().end(), dst.begin());
    });
}

template<class T>
[[nodiscard]] auto copy_device_to_device(
    queue& q,
    device_buffer<T>& dst,
    const device_buffer<T>& src) {
    return __detail::make_command_sender(q.state_, [&dst, &src] {
        if (dst.size() != src.size()) {
            throw operation_error{
                error_kind::size_mismatch,
                "forge::accel::cpu: D2D size mismatch"};
        }
        std::copy(src.span().begin(), src.span().end(), dst.span().begin());
    });
}

template<class F>
[[nodiscard]] auto submit(queue& q, F&& f) {
    return __detail::make_command_sender(q.state_, static_cast<F&&>(f));
}

[[nodiscard]] inline auto record_event(queue& q, event& ev) {
    return __detail::make_command_sender(q.state_, [ev = ev.state_] {
        if (!ev) {
            throw operation_error{
                error_kind::invalid_event,
                "forge::accel::cpu: record_event invalid event"};
        }
        const auto generation = ev->reserve_record_generation();
        ev->mark_completed(generation);
    });
}

[[nodiscard]] inline auto wait_event(
    queue& q,
    event& ev,
    event_wait_options options = {}) {
    auto queue_state = q.state_;
    auto event_state = ev.state_;
    return __detail::make_command_sender(queue_state, [queue_state, ev = std::move(event_state), timeout = options.timeout] {
        if (!ev) {
            throw operation_error{
                error_kind::invalid_event,
                "forge::accel::cpu: wait_event invalid event"};
        }
        auto state = queue_state ? queue_state->owner.lock() : nullptr;
        const auto target = ev->wait_target_generation();
        auto deadline = timeout
            ? std::optional<std::chrono::steady_clock::time_point>{
                  std::chrono::steady_clock::now() + *timeout}
            : std::nullopt;
        const auto status = __detail::wait_until_event_ready_or_stopped(
            state,
            ev,
            target,
            deadline);
        if (status == command_status::stopped) {
            throw __detail::__stopped_signal{};
        }
        if (status == command_status::timed_out) {
            throw command_error{command_status::timed_out};
        }
    });
}

[[nodiscard]] inline auto synchronize_event(
    queue& q,
    event& ev,
    event_wait_options options = {}) {
    auto queue_state = q.state_;
    auto event_state = ev.state_;
    return __detail::make_command_sender(queue_state, [queue_state, ev = std::move(event_state), timeout = options.timeout] {
        if (!ev) {
            throw operation_error{
                error_kind::invalid_event,
                "forge::accel::cpu: synchronize_event invalid event"};
        }
        auto state = queue_state ? queue_state->owner.lock() : nullptr;
        const auto target = ev->recorded();
        auto deadline = timeout
            ? std::optional<std::chrono::steady_clock::time_point>{
                  std::chrono::steady_clock::now() + *timeout}
            : std::nullopt;
        const auto status = __detail::wait_until_event_ready_or_stopped(
            state,
            ev,
            target,
            deadline);
        if (status == command_status::stopped) {
            throw __detail::__stopped_signal{};
        }
        if (status == command_status::timed_out) {
            throw command_error{command_status::timed_out};
        }
    });
}

[[nodiscard]] inline auto fence(queue& q) {
    return __detail::make_command_sender(q.state_, [] {});
}

[[nodiscard]] inline auto query_event(event& ev) {
    return std::execution::just(ev.query());
}

template<class T>
[[nodiscard]] auto copy_to_device_typed(
    queue& q,
    device_buffer<T>& dst,
    std::span<const T> src) {
    return __typed_detail::void_sender(copy_to_device(q, dst, src));
}

template<class T>
[[nodiscard]] auto copy_to_host_typed(
    queue& q,
    std::span<T> dst,
    const device_buffer<T>& src) {
    return __typed_detail::void_sender(copy_to_host(q, dst, src));
}

template<class T>
[[nodiscard]] auto copy_device_to_device_typed(
    queue& q,
    device_buffer<T>& dst,
    const device_buffer<T>& src) {
    return __typed_detail::void_sender(copy_device_to_device(q, dst, src));
}

template<class F>
[[nodiscard]] auto submit_typed(queue& q, F&& f) {
    return __typed_detail::void_sender(submit(q, static_cast<F&&>(f)));
}

[[nodiscard]] inline auto record_event_typed(queue& q, event& ev) {
    return __typed_detail::void_sender(record_event(q, ev));
}

[[nodiscard]] inline auto wait_event_typed(
    queue& q,
    event& ev,
    event_wait_options options = {}) {
    return __typed_detail::void_sender(wait_event(q, ev, options));
}

[[nodiscard]] inline auto synchronize_event_typed(
    queue& q,
    event& ev,
    event_wait_options options = {}) {
    return __typed_detail::void_sender(synchronize_event(q, ev, options));
}

[[nodiscard]] inline auto fence_typed(queue& q) {
    return __typed_detail::void_sender(fence(q));
}

[[nodiscard]] inline auto query_event_typed(event& ev) {
    return __typed_detail::value_sender<event_snapshot>(query_event(ev));
}

} // namespace forge::accel::cpu
