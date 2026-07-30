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

#include <concepts>
#include <cstddef>
#include <limits>
#include <memory>
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

static_assert(read_stream<any_read_stream>);
static_assert(write_stream<any_write_stream>);

} // namespace forge::io
