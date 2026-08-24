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

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace forge::io {

inline constexpr std::size_t dynamic_extent_limit =
    std::numeric_limits<std::size_t>::max();

namespace __memory_stream_detail {

[[nodiscard]] inline auto copy_to_vector(const_buffer input)
    -> std::vector<std::byte> {
    std::vector<std::byte> bytes(input.size());
    buffer_copy(
        mutable_buffer{bytes.data(), bytes.size()},
        input);
    return bytes;
}

[[nodiscard]] inline auto copy_to_vector(std::string_view input)
    -> std::vector<std::byte> {
    return copy_to_vector(const_buffer{input.data(), input.size()});
}

} // namespace __memory_stream_detail

class memory_read_stream {
public:
    memory_read_stream() noexcept = default;

    explicit memory_read_stream(
        const_buffer input,
        std::size_t max_read_size = dynamic_extent_limit) noexcept
        : input_(input)
        , max_read_size_(max_read_size)
    {}

    explicit memory_read_stream(
        std::string_view input,
        std::size_t max_read_size = dynamic_extent_limit) noexcept
        : memory_read_stream(
              const_buffer{input.data(), input.size()},
              max_read_size)
    {}

    template<class String>
        requires std::same_as<std::remove_cvref_t<String>, std::string> &&
                 (!std::is_lvalue_reference_v<String>)
    explicit memory_read_stream(
        String&& input,
        std::size_t max_read_size = dynamic_extent_limit) = delete;

    [[nodiscard]] auto position() const noexcept -> std::size_t {
        return position_;
    }

    [[nodiscard]] auto remaining() const noexcept -> std::size_t {
        return input_.size() - position_;
    }

    [[nodiscard]] auto eof() const noexcept -> bool {
        return remaining() == 0;
    }

    auto reset() noexcept -> void {
        position_ = 0;
    }

    [[nodiscard]] auto read_some(mutable_buffer output) noexcept
        -> io_result<std::size_t> {
        if (output.empty()) {
            return io_result<std::size_t>::success(0);
        }
        if (eof()) {
            return io_result<std::size_t>::end_of_file(0);
        }

        const auto transfer_limit = max_read_size_ == 0
            ? dynamic_extent_limit
            : max_read_size_;
        const auto count = std::min(
            {output.size(), remaining(), transfer_limit});

        buffer_copy(
            buffer_prefix(count, output),
            const_buffer{input_.data() + position_, count});
        position_ += count;
        return io_result<std::size_t>::success(count);
    }

private:
    const_buffer input_{};
    std::size_t position_ = 0;
    std::size_t max_read_size_ = dynamic_extent_limit;
};

class memory_write_stream {
public:
    memory_write_stream() = default;

    explicit memory_write_stream(std::size_t capacity)
        : capacity_(capacity) {
        if (capacity != dynamic_extent_limit) {
            storage_.reserve(capacity);
        }
    }

    explicit memory_write_stream(mutable_buffer output) noexcept
        : output_(output)
        , capacity_(output.size())
        , borrowed_(true)
    {}

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return borrowed_ ? position_ : storage_.size();
    }

    [[nodiscard]] auto capacity() const noexcept -> std::size_t {
        return capacity_;
    }

    [[nodiscard]] auto remaining_capacity() const noexcept -> std::size_t {
        if (capacity_ == dynamic_extent_limit) {
            return dynamic_extent_limit;
        }
        const auto current = size();
        return current < capacity_ ? capacity_ - current : 0;
    }

    [[nodiscard]] auto bytes() noexcept -> std::span<std::byte> {
        if (borrowed_) {
            return std::span<std::byte>{output_.data(), position_};
        }
        return std::span<std::byte>{storage_.data(), storage_.size()};
    }

    [[nodiscard]] auto bytes() const noexcept -> std::span<const std::byte> {
        if (borrowed_) {
            return std::span<const std::byte>{output_.data(), position_};
        }
        return std::span<const std::byte>{storage_.data(), storage_.size()};
    }

    auto clear() noexcept -> void {
        if (borrowed_) {
            position_ = 0;
        } else {
            storage_.clear();
        }
    }

    [[nodiscard]] auto write_some(const_buffer input)
        -> io_result<std::size_t> {
        if (input.empty()) {
            return io_result<std::size_t>::success(0);
        }

        const auto count = capacity_ == dynamic_extent_limit
            ? input.size()
            : std::min(input.size(), remaining_capacity());
        if (count == 0) {
            return io_result<std::size_t>::success(0);
        }

        if (borrowed_) {
            buffer_copy(
                mutable_buffer{output_.data() + position_, count},
                buffer_prefix(count, input));
            position_ += count;
        } else {
            const auto old_size = storage_.size();
            if (count > storage_.max_size() - old_size) {
                return io_result<std::size_t>::failure(
                    std::make_error_code(std::errc::value_too_large),
                    0);
            }
            std::vector<std::byte> staged_input;
            if (old_size != 0) {
                const auto less = std::less<const std::byte*>{};
                const auto* const begin = storage_.data();
                const auto* const end = begin + old_size;
                if (!less(input.data(), begin) && less(input.data(), end)) {
                    staged_input = __memory_stream_detail::copy_to_vector(
                        buffer_prefix(count, input));
                    input = const_buffer{
                        staged_input.data(),
                        staged_input.size()};
                }
            }
            storage_.resize(old_size + count);
            buffer_copy(
                mutable_buffer{storage_.data() + old_size, count},
                buffer_prefix(count, input));
        }

        return io_result<std::size_t>::success(count);
    }

private:
    mutable_buffer output_{};
    std::vector<std::byte> storage_{};
    std::size_t position_ = 0;
    std::size_t capacity_ = dynamic_extent_limit;
    bool borrowed_ = false;
};

class memory_stream {
public:
    memory_stream() = default;

    explicit memory_stream(const_buffer input) noexcept
        : reader_(input)
    {}

    explicit memory_stream(std::string_view input) noexcept
        : reader_(input)
    {}

    template<class String>
        requires std::same_as<std::remove_cvref_t<String>, std::string> &&
                 (!std::is_lvalue_reference_v<String>)
    explicit memory_stream(String&& input) = delete;

    memory_stream(const_buffer input, std::size_t write_capacity)
        : reader_(input)
        , writer_(write_capacity)
    {}

    memory_stream(const_buffer input, mutable_buffer output) noexcept
        : reader_(input)
        , writer_(output)
    {}

    [[nodiscard]] auto read_some(mutable_buffer output) noexcept
        -> io_result<std::size_t> {
        return reader_.read_some(output);
    }

    [[nodiscard]] auto write_some(const_buffer input)
        -> io_result<std::size_t> {
        return writer_.write_some(input);
    }

    [[nodiscard]] auto reader() noexcept -> memory_read_stream& {
        return reader_;
    }

    [[nodiscard]] auto reader() const noexcept -> const memory_read_stream& {
        return reader_;
    }

    [[nodiscard]] auto writer() noexcept -> memory_write_stream& {
        return writer_;
    }

    [[nodiscard]] auto writer() const noexcept -> const memory_write_stream& {
        return writer_;
    }

    [[nodiscard]] auto written_bytes() const noexcept
        -> std::span<const std::byte> {
        return writer_.bytes();
    }

private:
    memory_read_stream reader_{};
    memory_write_stream writer_{};
};

class scripted_read_step {
public:
    enum class kind {
        bytes,
        error,
        eof
    };

    [[nodiscard]] static auto bytes(const_buffer input)
        -> scripted_read_step {
        scripted_read_step step;
        step.kind_ = kind::bytes;
        step.bytes_ = __memory_stream_detail::copy_to_vector(input);
        return step;
    }

    [[nodiscard]] static auto bytes(std::string_view input)
        -> scripted_read_step {
        return bytes(const_buffer{input.data(), input.size()});
    }

    [[nodiscard]] static auto bytes_then_error(
        const_buffer input,
        std::error_code error) -> scripted_read_step {
        scripted_read_step step = bytes(input);
        step.error_ = std::move(error);
        step.completes_with_error_ = true;
        return step;
    }

    [[nodiscard]] static auto bytes_then_error(
        std::string_view input,
        std::error_code error) -> scripted_read_step {
        return bytes_then_error(
            const_buffer{input.data(), input.size()},
            std::move(error));
    }

    [[nodiscard]] static auto error(std::error_code error) -> scripted_read_step {
        scripted_read_step step;
        step.kind_ = kind::error;
        step.error_ = std::move(error);
        return step;
    }

    [[nodiscard]] static auto eof() noexcept -> scripted_read_step {
        scripted_read_step step;
        step.kind_ = kind::eof;
        return step;
    }

    [[nodiscard]] auto step_kind() const noexcept -> kind {
        return kind_;
    }

private:
    friend class scripted_read_stream;

    kind kind_ = kind::eof;
    std::vector<std::byte> bytes_{};
    std::size_t position_ = 0;
    std::error_code error_{};
    bool completes_with_error_ = false;
};

class scripted_read_stream {
public:
    scripted_read_stream() = default;

    scripted_read_stream(std::initializer_list<scripted_read_step> steps)
        : steps_(steps)
    {}

    auto push(scripted_read_step step) -> void {
        steps_.push_back(std::move(step));
    }

    [[nodiscard]] auto pending_steps() const noexcept -> std::size_t {
        return steps_.size();
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return steps_.empty();
    }

    [[nodiscard]] auto read_some(mutable_buffer output)
        -> io_result<std::size_t> {
        if (output.empty()) {
            return io_result<std::size_t>::success(0);
        }

        while (!steps_.empty()) {
            auto& step = steps_.front();
            switch (step.kind_) {
            case scripted_read_step::kind::eof:
                return io_result<std::size_t>::end_of_file(0);

            case scripted_read_step::kind::error: {
                const auto error = step.error_;
                steps_.pop_front();
                return io_result<std::size_t>::failure(error, 0);
            }

            case scripted_read_step::kind::bytes:
                if (step.position_ == step.bytes_.size() &&
                    !step.completes_with_error_) {
                    steps_.pop_front();
                    continue;
                }
                return read_bytes_step(output);
            }
        }

        return io_result<std::size_t>::end_of_file(0);
    }

private:
    [[nodiscard]] auto read_bytes_step(mutable_buffer output)
        -> io_result<std::size_t> {
        auto& step = steps_.front();
        const auto available = step.bytes_.size() - step.position_;
        if (available == 0) {
            const auto error = step.error_;
            const auto completes_with_error = step.completes_with_error_;
            steps_.pop_front();
            if (completes_with_error) {
                return io_result<std::size_t>::failure(error, 0);
            }
            return io_result<std::size_t>::end_of_file(0);
        }

        const auto source = const_buffer{
            step.bytes_.data() + step.position_,
            available};
        const auto copied = buffer_copy(output, source);
        step.position_ += copied;

        if (step.position_ == step.bytes_.size()) {
            const auto error = step.error_;
            const auto completes_with_error = step.completes_with_error_;
            steps_.pop_front();
            if (completes_with_error) {
                return io_result<std::size_t>::failure(error, copied);
            }
        }

        return io_result<std::size_t>::success(copied);
    }

    std::deque<scripted_read_step> steps_{};
};

} // namespace forge::io
