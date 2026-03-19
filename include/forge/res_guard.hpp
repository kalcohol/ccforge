// MIT License
//
// Copyright (c) 2026 Forge Project
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

#include <functional>
#include <utility>
#include <type_traits>

namespace forge {

/// @brief RAII resource guard for automatic resource management
/// @tparam T Resource type
template<typename T>
class res_guard {
public:
    using resource_type = T;
    using deleter_type = std::function<void(T&)>;

    res_guard() = delete;

    /// @brief Construct with resource and deleter
    template<typename U, typename D>
    res_guard(U&& resource, D&& deleter)
        : resource_(std::forward<U>(resource))
        , deleter_(std::forward<D>(deleter))
        , owned_(true) {}

    /// @brief Construct with resource creator and deleter
    template<typename Creator, typename D>
    res_guard(Creator&& creator, D&& deleter, std::enable_if_t<std::is_invocable_r_v<T, Creator>>* = nullptr)
        : resource_(std::forward<Creator>(creator)())
        , deleter_(std::forward<D>(deleter))
        , owned_(true) {}

    res_guard(const res_guard&) = delete;
    res_guard& operator=(const res_guard&) = delete;

    res_guard(res_guard&& other) noexcept
        : resource_(std::move(other.resource_))
        , deleter_(std::move(other.deleter_))
        , owned_(std::exchange(other.owned_, false)) {}

    res_guard& operator=(res_guard&& other) noexcept {
        if (this != &other) {
            reset();
            resource_ = std::move(other.resource_);
            deleter_ = std::move(other.deleter_);
            owned_ = std::exchange(other.owned_, false);
        }
        return *this;
    }

    ~res_guard() noexcept {
        reset();
    }

    /// @brief Get reference to managed resource
    T& get() noexcept { return resource_; }
    const T& get() const noexcept { return resource_; }

    /// @brief Check if guard owns a resource
    explicit operator bool() const noexcept { return owned_; }

    /// @brief Release ownership and return resource
    T release() noexcept {
        owned_ = false;
        return std::move(resource_);
    }

    /// @brief Reset with new resource
    template<typename U>
    void reset(U&& new_resource) {
        reset();
        resource_ = std::forward<U>(new_resource);
        owned_ = true;
    }

    /// @brief Reset and destroy current resource
    void reset() noexcept {
        if (owned_ && deleter_) {
            deleter_(resource_);
            owned_ = false;
        }
    }

    /// @brief Swap with another guard
    void swap(res_guard& other) noexcept {
        using std::swap;
        swap(resource_, other.resource_);
        swap(deleter_, other.deleter_);
        swap(owned_, other.owned_);
    }

private:
    T resource_;
    deleter_type deleter_;
    bool owned_;
};

template<typename T>
void swap(res_guard<T>& lhs, res_guard<T>& rhs) noexcept {
    lhs.swap(rhs);
}

} // namespace forge
