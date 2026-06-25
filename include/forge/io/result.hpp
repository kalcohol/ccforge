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

#include <concepts>
#include <cstddef>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace forge::io {

template<class... Ts>
class io_result {
public:
    io_result()
        requires ((std::default_initializable<Ts> && ...))
    = default;

    template<class... Us>
        requires (sizeof...(Us) == sizeof...(Ts))
              && ((std::constructible_from<Ts, Us&&> && ...))
    explicit(sizeof...(Ts) == 0)
    io_result(std::error_code error, Us&&... values)
        : error_(std::move(error))
        , values_(std::forward<Us>(values)...)
    {}

    template<class... Us>
        requires (sizeof...(Us) == sizeof...(Ts))
              && ((std::constructible_from<Ts, Us&&> && ...))
    [[nodiscard]] static auto success(Us&&... values) -> io_result {
        return io_result{{}, std::forward<Us>(values)...};
    }

    template<class... Us>
        requires (sizeof...(Us) == sizeof...(Ts))
              && ((std::constructible_from<Ts, Us&&> && ...))
    [[nodiscard]] static auto failure(std::error_code error, Us&&... values)
        -> io_result {
        return io_result{std::move(error), std::forward<Us>(values)...};
    }

    [[nodiscard]] auto error() & noexcept -> std::error_code& {
        return error_;
    }

    [[nodiscard]] auto error() const& noexcept -> const std::error_code& {
        return error_;
    }

    [[nodiscard]] auto error() && noexcept -> std::error_code&& {
        return std::move(error_);
    }

    [[nodiscard]] auto error() const&& noexcept -> const std::error_code&& {
        return std::move(error_);
    }

    [[nodiscard]] auto values() & noexcept -> std::tuple<Ts...>& {
        return values_;
    }

    [[nodiscard]] auto values() const& noexcept -> const std::tuple<Ts...>& {
        return values_;
    }

    [[nodiscard]] auto values() && noexcept -> std::tuple<Ts...>&& {
        return std::move(values_);
    }

    [[nodiscard]] auto values() const&& noexcept -> const std::tuple<Ts...>&& {
        return std::move(values_);
    }

    [[nodiscard]] auto has_value() const noexcept -> bool {
        return !error_;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

private:
    std::error_code error_{};
    std::tuple<Ts...> values_{};
};

namespace __result_detail {

template<std::size_t I, class... Ts>
struct tuple_element {
    using type = std::tuple_element_t<I - 1, std::tuple<Ts...>>;
};

template<class... Ts>
struct tuple_element<0, Ts...> {
    using type = std::error_code;
};

} // namespace __result_detail

template<std::size_t I, class... Ts>
[[nodiscard]] auto get(io_result<Ts...>& result) noexcept -> decltype(auto) {
    static_assert(I <= sizeof...(Ts), "io_result get index out of range");
    if constexpr (I == 0) {
        return (result.error());
    } else {
        return (std::get<I - 1>(result.values()));
    }
}

template<std::size_t I, class... Ts>
[[nodiscard]] auto get(const io_result<Ts...>& result) noexcept -> decltype(auto) {
    static_assert(I <= sizeof...(Ts), "io_result get index out of range");
    if constexpr (I == 0) {
        return (result.error());
    } else {
        return (std::get<I - 1>(result.values()));
    }
}

template<std::size_t I, class... Ts>
[[nodiscard]] auto get(io_result<Ts...>&& result) noexcept -> decltype(auto) {
    static_assert(I <= sizeof...(Ts), "io_result get index out of range");
    if constexpr (I == 0) {
        return std::move(result).error();
    } else {
        return std::get<I - 1>(std::move(result).values());
    }
}

template<std::size_t I, class... Ts>
[[nodiscard]] auto get(const io_result<Ts...>&& result) noexcept -> decltype(auto) {
    static_assert(I <= sizeof...(Ts), "io_result get index out of range");
    if constexpr (I == 0) {
        return std::move(result).error();
    } else {
        return std::get<I - 1>(std::move(result).values());
    }
}

} // namespace forge::io

template<class... Ts>
struct std::tuple_size<forge::io::io_result<Ts...>>
    : std::integral_constant<std::size_t, sizeof...(Ts) + 1> {};

template<std::size_t I, class... Ts>
struct std::tuple_element<I, forge::io::io_result<Ts...>>
    : forge::io::__result_detail::tuple_element<I, Ts...> {};
