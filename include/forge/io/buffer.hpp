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

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <ranges>
#include <span>
#include <type_traits>

namespace forge::io {

namespace __buffer_detail {

template<class T>
inline constexpr bool byte_like_v =
    std::is_same_v<std::remove_cv_t<T>, std::byte> ||
    std::is_same_v<std::remove_cv_t<T>, char> ||
    std::is_same_v<std::remove_cv_t<T>, signed char> ||
    std::is_same_v<std::remove_cv_t<T>, unsigned char>;

} // namespace __buffer_detail

class mutable_buffer {
public:
    mutable_buffer() noexcept = default;

    mutable_buffer(void* data, std::size_t size) noexcept
        : data_(static_cast<std::byte*>(data))
        , size_(size)
    {}

    template<class T, std::size_t Extent>
        requires __buffer_detail::byte_like_v<T> && (!std::is_const_v<T>)
    explicit mutable_buffer(std::span<T, Extent> bytes) noexcept
        : data_(reinterpret_cast<std::byte*>(bytes.data()))
        , size_(bytes.size_bytes())
    {}

    [[nodiscard]] auto data() const noexcept -> std::byte* {
        return data_;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return size_;
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return size_ == 0;
    }

private:
    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
};

class const_buffer {
public:
    const_buffer() noexcept = default;

    const_buffer(const void* data, std::size_t size) noexcept
        : data_(static_cast<const std::byte*>(data))
        , size_(size)
    {}

    const_buffer(mutable_buffer buffer) noexcept
        : data_(buffer.data())
        , size_(buffer.size())
    {}

    template<class T, std::size_t Extent>
        requires __buffer_detail::byte_like_v<T>
    explicit const_buffer(std::span<T, Extent> bytes) noexcept
        : data_(reinterpret_cast<const std::byte*>(bytes.data()))
        , size_(bytes.size_bytes())
    {}

    [[nodiscard]] auto data() const noexcept -> const std::byte* {
        return data_;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return size_;
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return size_ == 0;
    }

private:
    const std::byte* data_ = nullptr;
    std::size_t size_ = 0;
};

template<class R>
concept const_buffer_sequence =
    std::ranges::input_range<R> &&
    std::constructible_from<const_buffer, std::ranges::range_reference_t<R>>;

template<class R>
concept mutable_buffer_sequence =
    std::ranges::input_range<R> &&
    std::constructible_from<mutable_buffer, std::ranges::range_reference_t<R>>;

[[nodiscard]] inline auto buffer_size(const_buffer buffer) noexcept -> std::size_t {
    return buffer.size();
}

[[nodiscard]] inline auto buffer_size(mutable_buffer buffer) noexcept -> std::size_t {
    return buffer.size();
}

template<const_buffer_sequence Buffers>
[[nodiscard]] auto buffer_size(Buffers&& buffers) -> std::size_t {
    std::size_t total = 0;
    for (auto&& buffer : buffers) {
        total += const_buffer{static_cast<decltype(buffer)&&>(buffer)}.size();
    }
    return total;
}

[[nodiscard]] inline auto buffer_empty(const_buffer buffer) noexcept -> bool {
    return buffer.empty();
}

[[nodiscard]] inline auto buffer_empty(mutable_buffer buffer) noexcept -> bool {
    return buffer.empty();
}

template<const_buffer_sequence Buffers>
[[nodiscard]] auto buffer_empty(Buffers&& buffers) -> bool {
    return buffer_size(static_cast<Buffers&&>(buffers)) == 0;
}

[[nodiscard]] inline auto buffer_prefix(
    std::size_t max_size,
    const_buffer buffer) noexcept -> const_buffer {
    return const_buffer{buffer.data(), std::min(max_size, buffer.size())};
}

[[nodiscard]] inline auto buffer_prefix(
    std::size_t max_size,
    mutable_buffer buffer) noexcept -> mutable_buffer {
    return mutable_buffer{buffer.data(), std::min(max_size, buffer.size())};
}

inline auto buffer_copy(mutable_buffer dest, const_buffer source) noexcept
    -> std::size_t {
    const auto count = std::min(dest.size(), source.size());
    if (count != 0) {
        std::memcpy(dest.data(), source.data(), count);
    }
    return count;
}

template<mutable_buffer_sequence Dests, const_buffer_sequence Sources>
auto buffer_copy(Dests&& dests, Sources&& sources) -> std::size_t {
    auto dest_it = std::ranges::begin(dests);
    auto dest_last = std::ranges::end(dests);
    auto source_it = std::ranges::begin(sources);
    auto source_last = std::ranges::end(sources);

    mutable_buffer dest{};
    const_buffer source{};
    bool have_dest = false;
    bool have_source = false;
    std::size_t total = 0;

    while (true) {
        while (!have_dest || dest.empty()) {
            if (dest_it == dest_last) {
                return total;
            }
            dest = mutable_buffer{*dest_it};
            ++dest_it;
            have_dest = true;
        }

        while (!have_source || source.empty()) {
            if (source_it == source_last) {
                return total;
            }
            source = const_buffer{*source_it};
            ++source_it;
            have_source = true;
        }

        const auto copied = buffer_copy(dest, source);
        total += copied;
        dest = mutable_buffer{dest.data() + copied, dest.size() - copied};
        source = const_buffer{source.data() + copied, source.size() - copied};
    }
}

template<const_buffer_sequence Sources>
auto buffer_copy(mutable_buffer dest, Sources&& sources) -> std::size_t {
    mutable_buffer dests[] = {dest};
    return buffer_copy(dests, static_cast<Sources&&>(sources));
}

template<mutable_buffer_sequence Dests>
auto buffer_copy(Dests&& dests, const_buffer source) -> std::size_t {
    const_buffer sources[] = {source};
    return buffer_copy(static_cast<Dests&&>(dests), sources);
}

} // namespace forge::io
