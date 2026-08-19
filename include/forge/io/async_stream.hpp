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

#include <forge/io/coro.hpp>
#include <forge/io/stream.hpp>
#include <forge/resource_policy.hpp>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <exception>
#include <memory>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>
#endif

namespace forge::io {

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

inline constexpr std::size_t erased_io_awaitable_size = 128;
inline constexpr std::size_t erased_io_awaitable_alignment =
    alignof(std::max_align_t);

namespace __async_stream_detail {

template<class Awaitable>
concept io_size_result_awaitable =
    std::is_object_v<Awaitable> &&
    std::destructible<Awaitable> &&
    io_awaitable<Awaitable> &&
    std::same_as<
        decltype(std::declval<Awaitable&>().await_resume()),
        io_result<std::size_t>>;

template<class Awaitable>
concept erasable_io_size_result_awaitable =
    io_size_result_awaitable<Awaitable> &&
    (sizeof(Awaitable) <= erased_io_awaitable_size) &&
    (alignof(Awaitable) <= erased_io_awaitable_alignment);

class erased_awaitable_slot;

template<class Result>
class immediate_result_awaitable {
public:
    explicit immediate_result_awaitable(Result result)
        : result_(std::move(result))
    {}

    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return true;
    }

    auto await_suspend(std::coroutine_handle<>, const io_env*) const noexcept
        -> bool {
        return false;
    }

    [[nodiscard]] auto await_resume() -> Result {
        return std::move(result_);
    }

private:
    Result result_;
};

} // namespace __async_stream_detail

template<class Stream>
using async_read_awaitable_t = decltype(
    std::declval<Stream&>().read_some(mutable_buffer{}));

template<class Stream>
using async_write_awaitable_t = decltype(
    std::declval<Stream&>().write_some(const_buffer{}));

template<class Stream>
concept async_read_stream =
    requires(Stream& stream) {
        stream.read_some(mutable_buffer{});
    } &&
    std::same_as<
        async_read_awaitable_t<Stream>,
        std::remove_cvref_t<async_read_awaitable_t<Stream>>> &&
    __async_stream_detail::io_size_result_awaitable<
        async_read_awaitable_t<Stream>> &&
    (!requires(Stream& stream, const_buffer input) {
        stream.read_some(input);
    }) &&
    (!requires(Stream& stream) {
        stream.read_some(const_buffer{});
    });

template<class Stream>
concept async_write_stream =
    requires(Stream& stream) {
        stream.write_some(const_buffer{});
    } &&
    std::same_as<
        async_write_awaitable_t<Stream>,
        std::remove_cvref_t<async_write_awaitable_t<Stream>>> &&
    __async_stream_detail::io_size_result_awaitable<
        async_write_awaitable_t<Stream>>;

template<class Stream>
concept async_read_write_stream =
    async_read_stream<Stream> && async_write_stream<Stream>;

template<class Stream>
class immediate_async_stream {
public:
    template<class Source>
        requires std::same_as<std::remove_cvref_t<Source>, Stream>
              && std::constructible_from<Stream, Source>
    explicit immediate_async_stream(Source&& stream)
        : stream_(std::forward<Source>(stream))
    {}

    immediate_async_stream(const immediate_async_stream&) = default;
    auto operator=(const immediate_async_stream&)
        -> immediate_async_stream& = default;
    immediate_async_stream(immediate_async_stream&&) = default;
    auto operator=(immediate_async_stream&&)
        -> immediate_async_stream& = default;

    [[nodiscard]] auto underlying() noexcept -> Stream& {
        return stream_;
    }

    [[nodiscard]] auto underlying() const noexcept -> const Stream& {
        return stream_;
    }

    [[nodiscard]] auto read_some(mutable_buffer output)
        requires read_stream<Stream>
    {
        return __async_stream_detail::immediate_result_awaitable{
            stream_.read_some(output)};
    }

    [[nodiscard]] auto write_some(const_buffer input)
        requires write_stream<Stream>
    {
        return __async_stream_detail::immediate_result_awaitable{
            stream_.write_some(input)};
    }

private:
    Stream stream_;
};

template<class Stream>
immediate_async_stream(Stream&&)
    -> immediate_async_stream<std::remove_cvref_t<Stream>>;

class erased_io_awaitable {
public:
    erased_io_awaitable(const erased_io_awaitable&) = delete;
    auto operator=(const erased_io_awaitable&) -> erased_io_awaitable& = delete;

    erased_io_awaitable(erased_io_awaitable&& other) noexcept
        : slot_(std::exchange(other.slot_, nullptr))
        , immediate_result_(std::move(other.immediate_result_))
        , started_(other.started_)
    {}

    auto operator=(erased_io_awaitable&&) -> erased_io_awaitable& = delete;

    ~erased_io_awaitable();

    [[nodiscard]] auto await_ready() -> bool;

    auto await_suspend(
        std::coroutine_handle<> continuation,
        const io_env* env) -> std::coroutine_handle<>;

    [[nodiscard]] auto await_resume() -> io_result<std::size_t>;

private:
    friend class __async_stream_detail::erased_awaitable_slot;
    friend class owning_any_async_read_stream;
    friend class owning_any_async_write_stream;

    explicit erased_io_awaitable(
        __async_stream_detail::erased_awaitable_slot& slot) noexcept
        : slot_(std::addressof(slot))
    {}

    explicit erased_io_awaitable(io_result<std::size_t> result) noexcept
        : immediate_result_(std::move(result))
    {}

    auto abandon_unstarted() noexcept -> void;

    __async_stream_detail::erased_awaitable_slot* slot_ = nullptr;
    io_result<std::size_t> immediate_result_{};
    bool started_ = false;
};

namespace __async_stream_detail {

class erased_awaitable_slot {
public:
    erased_awaitable_slot() noexcept = default;
    erased_awaitable_slot(const erased_awaitable_slot&) = delete;
    auto operator=(const erased_awaitable_slot&)
        -> erased_awaitable_slot& = delete;

    [[nodiscard]] auto active() const noexcept -> bool {
        return active_.load(std::memory_order_acquire);
    }

    template<class AwaitableFactory>
    [[nodiscard]] auto emplace(AwaitableFactory&& factory)
        -> erased_io_awaitable {
        using awaitable_t = std::invoke_result_t<AwaitableFactory>;
        static_assert(
            erasable_io_size_result_awaitable<awaitable_t>,
            "async stream awaitable exceeds the erased operation slot");

        bool expected = false;
        if (!active_.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return erased_io_awaitable{
                io_result<std::size_t>::failure(
                    std::make_error_code(std::errc::operation_in_progress),
                    0)};
        }

        try {
            ::new (storage()) awaitable_t(
                std::forward<AwaitableFactory>(factory)());
            operations_ = std::addressof(operations_for<awaitable_t>);
        } catch (...) {
            active_.store(false, std::memory_order_release);
            throw;
        }
        return erased_io_awaitable{*this};
    }

    [[nodiscard]] auto await_ready() -> bool {
        return operations_->await_ready(storage());
    }

    auto await_suspend(
        std::coroutine_handle<> continuation,
        const io_env* env) -> std::coroutine_handle<> {
        return operations_->await_suspend(storage(), continuation, env);
    }

    [[nodiscard]] auto await_resume() -> io_result<std::size_t> {
        return operations_->await_resume(storage());
    }

    auto reset() noexcept -> void {
        operations_->destroy(storage());
        operations_ = nullptr;
        active_.store(false, std::memory_order_release);
    }

private:
    struct operations {
        bool (*await_ready)(void*);
        std::coroutine_handle<> (*await_suspend)(
            void*,
            std::coroutine_handle<>,
            const io_env*);
        io_result<std::size_t> (*await_resume)(void*);
        void (*destroy)(void*) noexcept;
    };

    template<class Awaitable>
    [[nodiscard]] static auto await_ready_model(void* object) -> bool {
        return static_cast<bool>(
            static_cast<Awaitable*>(object)->await_ready());
    }

    template<class Awaitable>
    static auto await_suspend_model(
        void* object,
        std::coroutine_handle<> continuation,
        const io_env* env) -> std::coroutine_handle<> {
        auto& awaitable = *static_cast<Awaitable*>(object);
        using result_t = decltype(
            awaitable.await_suspend(continuation, env));
        if constexpr (std::same_as<result_t, void>) {
            awaitable.await_suspend(continuation, env);
            return std::noop_coroutine();
        } else if constexpr (std::same_as<result_t, bool>) {
            return awaitable.await_suspend(continuation, env)
                ? std::noop_coroutine()
                : continuation;
        } else {
            return awaitable.await_suspend(continuation, env);
        }
    }

    template<class Awaitable>
    [[nodiscard]] static auto await_resume_model(void* object)
        -> io_result<std::size_t> {
        return static_cast<Awaitable*>(object)->await_resume();
    }

    template<class Awaitable>
    static auto destroy_model(void* object) noexcept -> void {
        std::destroy_at(static_cast<Awaitable*>(object));
    }

    template<class Awaitable>
    inline static constexpr operations operations_for{
        &await_ready_model<Awaitable>,
        &await_suspend_model<Awaitable>,
        &await_resume_model<Awaitable>,
        &destroy_model<Awaitable>};

    [[nodiscard]] auto storage() noexcept -> void* {
        return static_cast<void*>(storage_);
    }

    alignas(std::max_align_t) std::byte storage_[erased_io_awaitable_size]{};
    const operations* operations_ = nullptr;
    std::atomic_bool active_ = false;
};

} // namespace __async_stream_detail

inline erased_io_awaitable::~erased_io_awaitable() {
    // Abandoning a started-but-unresumed erased operation would leave the
    // slot permanently active (every later stream call fails with
    // operation_in_progress and the stream destructor terminates) while the
    // wrapped awaitable still holds the destroyed coroutine's continuation,
    // so a later backend completion would resume freed memory. Fail fast,
    // same shape as the io_uring abandonment guard. await_resume() clears
    // slot_, so started_ with a live slot means the operation is pending.
    if (slot_ != nullptr && started_) {
        std::terminate();
    }
    abandon_unstarted();
}

inline auto erased_io_awaitable::abandon_unstarted() noexcept -> void {
    if (slot_ != nullptr && !started_) {
        slot_->reset();
    }
    slot_ = nullptr;
}

inline auto erased_io_awaitable::await_ready() -> bool {
    if (slot_ == nullptr) {
        return true;
    }

    started_ = true;
    try {
        return slot_->await_ready();
    } catch (...) {
        auto* slot = std::exchange(slot_, nullptr);
        slot->reset();
        throw;
    }
}

inline auto erased_io_awaitable::await_suspend(
    std::coroutine_handle<> continuation,
    const io_env* env) -> std::coroutine_handle<> {
    if (slot_ == nullptr) {
        return continuation;
    }

    started_ = true;
    try {
        return slot_->await_suspend(continuation, env);
    } catch (...) {
        // Structural invariant: wrapped awaitables must not throw once they
        // have handed the operation to the kernel (their submission tails
        // are noexcept). reset() destroys the underlying awaitable, which
        // would otherwise leave an in-flight completion pointing at freed
        // memory. A throw here therefore means the operation never started.
        auto* slot = std::exchange(slot_, nullptr);
        slot->reset();
        throw;
    }
}

inline auto erased_io_awaitable::await_resume()
    -> io_result<std::size_t> {
    if (slot_ == nullptr) {
        return std::move(immediate_result_);
    }

    auto* slot = std::exchange(slot_, nullptr);
    try {
        auto result = slot->await_resume();
        slot->reset();
        return result;
    } catch (...) {
        slot->reset();
        throw;
    }
}

namespace __async_stream_detail {

template<class Stream>
concept erasable_async_read_stream =
    async_read_stream<Stream> &&
    erasable_io_size_result_awaitable<async_read_awaitable_t<Stream>>;

template<class Stream>
concept erasable_async_write_stream =
    async_write_stream<Stream> &&
    erasable_io_size_result_awaitable<async_write_awaitable_t<Stream>>;

} // namespace __async_stream_detail

class owning_any_async_read_stream {
public:
    owning_any_async_read_stream() noexcept = default;

    template<class Stream>
        requires (!std::same_as<
                  std::remove_cvref_t<Stream>,
                  owning_any_async_read_stream>)
              && __async_stream_detail::erasable_async_read_stream<
                     std::remove_cvref_t<Stream>>
              && std::constructible_from<std::remove_cvref_t<Stream>, Stream>
    explicit owning_any_async_read_stream(
        Stream&& stream,
        std::pmr::memory_resource* memory = forge::default_memory_resource())
        : memory_(forge::normalize_memory_resource(memory)) {
        using stream_t = std::remove_cvref_t<Stream>;
        object_ = memory_->allocate(sizeof(stream_t), alignof(stream_t));
        try {
            std::construct_at(
                static_cast<stream_t*>(object_),
                std::forward<Stream>(stream));
        } catch (...) {
            memory_->deallocate(object_, sizeof(stream_t), alignof(stream_t));
            object_ = nullptr;
            throw;
        }
        operations_ = std::addressof(operations_for<stream_t>);
    }

    owning_any_async_read_stream(
        const owning_any_async_read_stream&) = delete;
    auto operator=(const owning_any_async_read_stream&)
        -> owning_any_async_read_stream& = delete;

    owning_any_async_read_stream(owning_any_async_read_stream&& other) {
        if (other.operation_.active()) {
            throw std::logic_error{
                "cannot move an async read stream with an active operation"};
        }
        take_from(other);
    }

    auto operator=(owning_any_async_read_stream&& other)
        -> owning_any_async_read_stream& {
        if (this != std::addressof(other)) {
            require_inactive();
            other.require_inactive();
            reset_object();
            take_from(other);
        }
        return *this;
    }

    ~owning_any_async_read_stream() {
        if (operation_.active()) {
            std::terminate();
        }
        reset_object();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return operations_ != nullptr;
    }

    [[nodiscard]] auto get_memory_resource() const noexcept
        -> std::pmr::memory_resource* {
        return memory_;
    }

    auto reset() -> void {
        require_inactive();
        reset_object();
    }

    [[nodiscard]] auto read_some(mutable_buffer output)
        -> erased_io_awaitable {
        if (operations_ == nullptr) {
            return erased_io_awaitable{
                io_result<std::size_t>::failure(
                    std::make_error_code(std::errc::bad_address),
                    0)};
        }
        return operations_->read(object_, output, operation_);
    }

private:
    struct operations {
        erased_io_awaitable (*read)(
            void*,
            mutable_buffer,
            __async_stream_detail::erased_awaitable_slot&);
        void (*destroy)(void*, std::pmr::memory_resource*) noexcept;
    };

    template<class Stream>
    [[nodiscard]] static auto read_model(
        void* object,
        mutable_buffer output,
        __async_stream_detail::erased_awaitable_slot& operation)
            -> erased_io_awaitable {
        using awaitable_t = async_read_awaitable_t<Stream>;
        return operation.emplace([object, output]() -> awaitable_t {
            return static_cast<Stream*>(object)->read_some(output);
        });
    }

    template<class Stream>
    static auto destroy_model(
        void* object,
        std::pmr::memory_resource* memory) noexcept -> void {
        std::destroy_at(static_cast<Stream*>(object));
        memory->deallocate(object, sizeof(Stream), alignof(Stream));
    }

    template<class Stream>
    inline static constexpr operations operations_for{
        &read_model<Stream>,
        &destroy_model<Stream>};

    auto require_inactive() const -> void {
        if (operation_.active()) {
            throw std::logic_error{
                "async read stream has an active operation"};
        }
    }

    auto reset_object() noexcept -> void {
        if (operations_ != nullptr) {
            operations_->destroy(object_, memory_);
            object_ = nullptr;
            operations_ = nullptr;
        }
    }

    auto take_from(owning_any_async_read_stream& other) noexcept -> void {
        object_ = std::exchange(other.object_, nullptr);
        operations_ = std::exchange(other.operations_, nullptr);
        memory_ = other.memory_;
    }

    void* object_ = nullptr;
    const operations* operations_ = nullptr;
    std::pmr::memory_resource* memory_ = forge::default_memory_resource();
    __async_stream_detail::erased_awaitable_slot operation_{};
};

class owning_any_async_write_stream {
public:
    owning_any_async_write_stream() noexcept = default;

    template<class Stream>
        requires (!std::same_as<
                  std::remove_cvref_t<Stream>,
                  owning_any_async_write_stream>)
              && __async_stream_detail::erasable_async_write_stream<
                     std::remove_cvref_t<Stream>>
              && std::constructible_from<std::remove_cvref_t<Stream>, Stream>
    explicit owning_any_async_write_stream(
        Stream&& stream,
        std::pmr::memory_resource* memory = forge::default_memory_resource())
        : memory_(forge::normalize_memory_resource(memory)) {
        using stream_t = std::remove_cvref_t<Stream>;
        object_ = memory_->allocate(sizeof(stream_t), alignof(stream_t));
        try {
            std::construct_at(
                static_cast<stream_t*>(object_),
                std::forward<Stream>(stream));
        } catch (...) {
            memory_->deallocate(object_, sizeof(stream_t), alignof(stream_t));
            object_ = nullptr;
            throw;
        }
        operations_ = std::addressof(operations_for<stream_t>);
    }

    owning_any_async_write_stream(
        const owning_any_async_write_stream&) = delete;
    auto operator=(const owning_any_async_write_stream&)
        -> owning_any_async_write_stream& = delete;

    owning_any_async_write_stream(owning_any_async_write_stream&& other) {
        if (other.operation_.active()) {
            throw std::logic_error{
                "cannot move an async write stream with an active operation"};
        }
        take_from(other);
    }

    auto operator=(owning_any_async_write_stream&& other)
        -> owning_any_async_write_stream& {
        if (this != std::addressof(other)) {
            require_inactive();
            other.require_inactive();
            reset_object();
            take_from(other);
        }
        return *this;
    }

    ~owning_any_async_write_stream() {
        if (operation_.active()) {
            std::terminate();
        }
        reset_object();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return operations_ != nullptr;
    }

    [[nodiscard]] auto get_memory_resource() const noexcept
        -> std::pmr::memory_resource* {
        return memory_;
    }

    auto reset() -> void {
        require_inactive();
        reset_object();
    }

    [[nodiscard]] auto write_some(const_buffer input)
        -> erased_io_awaitable {
        if (operations_ == nullptr) {
            return erased_io_awaitable{
                io_result<std::size_t>::failure(
                    std::make_error_code(std::errc::bad_address),
                    0)};
        }
        return operations_->write(object_, input, operation_);
    }

private:
    struct operations {
        erased_io_awaitable (*write)(
            void*,
            const_buffer,
            __async_stream_detail::erased_awaitable_slot&);
        void (*destroy)(void*, std::pmr::memory_resource*) noexcept;
    };

    template<class Stream>
    [[nodiscard]] static auto write_model(
        void* object,
        const_buffer input,
        __async_stream_detail::erased_awaitable_slot& operation)
            -> erased_io_awaitable {
        using awaitable_t = async_write_awaitable_t<Stream>;
        return operation.emplace([object, input]() -> awaitable_t {
            return static_cast<Stream*>(object)->write_some(input);
        });
    }

    template<class Stream>
    static auto destroy_model(
        void* object,
        std::pmr::memory_resource* memory) noexcept -> void {
        std::destroy_at(static_cast<Stream*>(object));
        memory->deallocate(object, sizeof(Stream), alignof(Stream));
    }

    template<class Stream>
    inline static constexpr operations operations_for{
        &write_model<Stream>,
        &destroy_model<Stream>};

    auto require_inactive() const -> void {
        if (operation_.active()) {
            throw std::logic_error{
                "async write stream has an active operation"};
        }
    }

    auto reset_object() noexcept -> void {
        if (operations_ != nullptr) {
            operations_->destroy(object_, memory_);
            object_ = nullptr;
            operations_ = nullptr;
        }
    }

    auto take_from(owning_any_async_write_stream& other) noexcept -> void {
        object_ = std::exchange(other.object_, nullptr);
        operations_ = std::exchange(other.operations_, nullptr);
        memory_ = other.memory_;
    }

    void* object_ = nullptr;
    const operations* operations_ = nullptr;
    std::pmr::memory_resource* memory_ = forge::default_memory_resource();
    __async_stream_detail::erased_awaitable_slot operation_{};
};

static_assert(async_read_stream<owning_any_async_read_stream>);
static_assert(async_write_stream<owning_any_async_write_stream>);

#endif // __cpp_impl_coroutine

} // namespace forge::io
