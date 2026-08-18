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

#include <forge/io/buffer.hpp>
#include <forge/io/result.hpp>
#include <forge/resource_policy.hpp>

#include <concepts>
#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace forge::io {

template<class Stream>
concept read_stream =
    requires(Stream& stream) {
        { stream.read_some(mutable_buffer{}) } -> std::same_as<io_result<std::size_t>>;
    } &&
    (!requires(Stream& stream, const_buffer input) {
        stream.read_some(input);
    }) &&
    (!requires(Stream& stream) {
        stream.read_some(const_buffer{});
    });

template<class Stream>
concept write_stream =
    requires(Stream& stream) {
        { stream.write_some(const_buffer{}) } -> std::same_as<io_result<std::size_t>>;
    };

template<class Stream>
concept read_write_stream = read_stream<Stream> && write_stream<Stream>;

namespace __stream_detail {

[[nodiscard]] inline auto short_transfer_error() -> std::error_code {
    return std::make_error_code(std::errc::io_error);
}

[[nodiscard]] inline auto record_too_large_error() -> std::error_code {
    return std::make_error_code(std::errc::message_size);
}

} // namespace __stream_detail

template<read_stream Stream>
[[nodiscard]] auto read_exactly(Stream& stream, mutable_buffer output)
    -> io_result<std::size_t> {
    std::size_t total = 0;
    while (total < output.size()) {
        auto result = stream.read_some(
            mutable_buffer{output.data() + total, output.size() - total});
        auto [error, count] = result;
        total += count;

        if (error) {
            return io_result<std::size_t>::failure(error, total);
        }
        if (total == output.size()) {
            return io_result<std::size_t>::success(total);
        }
        if (result.eof() || count == 0) {
            return io_result<std::size_t>::end_of_file(total);
        }
    }

    return io_result<std::size_t>::success(total);
}

template<write_stream Stream>
[[nodiscard]] auto write_all(Stream& stream, const_buffer input)
    -> io_result<std::size_t> {
    std::size_t total = 0;
    while (total < input.size()) {
        auto [error, count] = stream.write_some(
            const_buffer{input.data() + total, input.size() - total});
        total += count;

        if (error) {
            return io_result<std::size_t>::failure(error, total);
        }
        if (count == 0) {
            return io_result<std::size_t>::failure(
                __stream_detail::short_transfer_error(),
                total);
        }
    }

    return io_result<std::size_t>::success(total);
}

template<read_stream Stream>
[[nodiscard]] auto read_until(
    Stream& stream,
    std::string& output,
    char delimiter = '\n',
    std::size_t max_bytes = std::numeric_limits<std::size_t>::max())
        -> io_result<std::size_t> {
    output.clear();

    std::size_t total = 0;
    while (total < max_bytes) {
        std::byte byte{};
        auto result =
            stream.read_some(mutable_buffer{std::addressof(byte), 1});
        auto [error, count] = result;

        if (count > 0) {
            ++total;
            const auto ch =
                static_cast<char>(std::to_integer<unsigned char>(byte));
            output.push_back(ch);
        }

        if (error) {
            return io_result<std::size_t>::failure(error, total);
        }
        if (count > 0 && output.back() == delimiter) {
            return io_result<std::size_t>::success(total);
        }
        if (result.eof() || count == 0) {
            return io_result<std::size_t>::end_of_file(total);
        }
    }

    return io_result<std::size_t>::failure(
        __stream_detail::record_too_large_error(),
        total);
}

class any_read_stream {
public:
    any_read_stream() noexcept = default;

    template<class Stream>
        requires read_stream<Stream>
              && (!std::is_same_v<std::remove_cvref_t<Stream>, any_read_stream>)
    explicit any_read_stream(Stream& stream) noexcept
        : object_(std::addressof(stream))
        , read_(&read_model<Stream>)
    {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return read_ != nullptr;
    }

    auto reset() noexcept -> void {
        object_ = nullptr;
        read_ = nullptr;
    }

    [[nodiscard]] auto read_some(mutable_buffer output)
        -> io_result<std::size_t> {
        if (read_ == nullptr) {
            return io_result<std::size_t>::failure(
                std::make_error_code(std::errc::bad_address),
                0);
        }
        return read_(object_, output);
    }

private:
    template<class Stream>
    [[nodiscard]] static auto read_model(void* object, mutable_buffer output)
        -> io_result<std::size_t> {
        return static_cast<Stream*>(object)->read_some(output);
    }

    void* object_ = nullptr;
    io_result<std::size_t> (*read_)(void*, mutable_buffer) = nullptr;
};

class any_write_stream {
public:
    any_write_stream() noexcept = default;

    template<class Stream>
        requires write_stream<Stream>
              && (!std::is_same_v<std::remove_cvref_t<Stream>, any_write_stream>)
    explicit any_write_stream(Stream& stream) noexcept
        : object_(std::addressof(stream))
        , write_(&write_model<Stream>)
    {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return write_ != nullptr;
    }

    auto reset() noexcept -> void {
        object_ = nullptr;
        write_ = nullptr;
    }

    [[nodiscard]] auto write_some(const_buffer input)
        -> io_result<std::size_t> {
        if (write_ == nullptr) {
            return io_result<std::size_t>::failure(
                std::make_error_code(std::errc::bad_address),
                0);
        }
        return write_(object_, input);
    }

private:
    template<class Stream>
    [[nodiscard]] static auto write_model(void* object, const_buffer input)
        -> io_result<std::size_t> {
        return static_cast<Stream*>(object)->write_some(input);
    }

    void* object_ = nullptr;
    io_result<std::size_t> (*write_)(void*, const_buffer) = nullptr;
};

class owning_any_read_stream {
public:
    owning_any_read_stream() noexcept = default;

    template<class Stream>
        requires (!std::same_as<
                  std::remove_cvref_t<Stream>,
                  owning_any_read_stream>)
              && read_stream<std::remove_cvref_t<Stream>>
              && std::constructible_from<std::remove_cvref_t<Stream>, Stream>
    explicit owning_any_read_stream(
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

    owning_any_read_stream(const owning_any_read_stream&) = delete;
    auto operator=(const owning_any_read_stream&)
        -> owning_any_read_stream& = delete;

    owning_any_read_stream(owning_any_read_stream&& other) noexcept
        : object_(std::exchange(other.object_, nullptr))
        , operations_(std::exchange(other.operations_, nullptr))
        , memory_(other.memory_)
    {}

    auto operator=(owning_any_read_stream&& other) noexcept
        -> owning_any_read_stream& {
        if (this != std::addressof(other)) {
            reset();
            object_ = std::exchange(other.object_, nullptr);
            operations_ = std::exchange(other.operations_, nullptr);
            memory_ = other.memory_;
        }
        return *this;
    }

    ~owning_any_read_stream() {
        reset();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return operations_ != nullptr;
    }

    [[nodiscard]] auto get_memory_resource() const noexcept
        -> std::pmr::memory_resource* {
        return memory_;
    }

    auto reset() noexcept -> void {
        if (operations_ != nullptr) {
            operations_->destroy(object_, memory_);
            object_ = nullptr;
            operations_ = nullptr;
        }
    }

    [[nodiscard]] auto read_some(mutable_buffer output)
        -> io_result<std::size_t> {
        if (operations_ == nullptr) {
            return io_result<std::size_t>::failure(
                std::make_error_code(std::errc::bad_address),
                0);
        }
        return operations_->read(object_, output);
    }

private:
    struct operations {
        io_result<std::size_t> (*read)(void*, mutable_buffer);
        void (*destroy)(void*, std::pmr::memory_resource*) noexcept;
    };

    template<class Stream>
    [[nodiscard]] static auto read_model(void* object, mutable_buffer output)
        -> io_result<std::size_t> {
        return static_cast<Stream*>(object)->read_some(output);
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

    void* object_ = nullptr;
    const operations* operations_ = nullptr;
    std::pmr::memory_resource* memory_ = forge::default_memory_resource();
};

class owning_any_write_stream {
public:
    owning_any_write_stream() noexcept = default;

    template<class Stream>
        requires (!std::same_as<
                  std::remove_cvref_t<Stream>,
                  owning_any_write_stream>)
              && write_stream<std::remove_cvref_t<Stream>>
              && std::constructible_from<std::remove_cvref_t<Stream>, Stream>
    explicit owning_any_write_stream(
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

    owning_any_write_stream(const owning_any_write_stream&) = delete;
    auto operator=(const owning_any_write_stream&)
        -> owning_any_write_stream& = delete;

    owning_any_write_stream(owning_any_write_stream&& other) noexcept
        : object_(std::exchange(other.object_, nullptr))
        , operations_(std::exchange(other.operations_, nullptr))
        , memory_(other.memory_)
    {}

    auto operator=(owning_any_write_stream&& other) noexcept
        -> owning_any_write_stream& {
        if (this != std::addressof(other)) {
            reset();
            object_ = std::exchange(other.object_, nullptr);
            operations_ = std::exchange(other.operations_, nullptr);
            memory_ = other.memory_;
        }
        return *this;
    }

    ~owning_any_write_stream() {
        reset();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return operations_ != nullptr;
    }

    [[nodiscard]] auto get_memory_resource() const noexcept
        -> std::pmr::memory_resource* {
        return memory_;
    }

    auto reset() noexcept -> void {
        if (operations_ != nullptr) {
            operations_->destroy(object_, memory_);
            object_ = nullptr;
            operations_ = nullptr;
        }
    }

    [[nodiscard]] auto write_some(const_buffer input)
        -> io_result<std::size_t> {
        if (operations_ == nullptr) {
            return io_result<std::size_t>::failure(
                std::make_error_code(std::errc::bad_address),
                0);
        }
        return operations_->write(object_, input);
    }

private:
    struct operations {
        io_result<std::size_t> (*write)(void*, const_buffer);
        void (*destroy)(void*, std::pmr::memory_resource*) noexcept;
    };

    template<class Stream>
    [[nodiscard]] static auto write_model(void* object, const_buffer input)
        -> io_result<std::size_t> {
        return static_cast<Stream*>(object)->write_some(input);
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

    void* object_ = nullptr;
    const operations* operations_ = nullptr;
    std::pmr::memory_resource* memory_ = forge::default_memory_resource();
};

static_assert(read_stream<any_read_stream>);
static_assert(write_stream<any_write_stream>);
static_assert(read_stream<owning_any_read_stream>);
static_assert(write_stream<owning_any_write_stream>);

} // namespace forge::io
