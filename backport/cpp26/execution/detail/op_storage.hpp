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

#include <cstddef>
#include <new>
#include <utility>

namespace std::execution::__forge_detail {

template<std::size_t SmallSize = 1024,
         std::size_t Align = alignof(std::max_align_t)>
class __op_storage {
public:
    __op_storage() noexcept = default;
    __op_storage(const __op_storage&) = delete;
    __op_storage(__op_storage&&) = delete;
    __op_storage& operator=(const __op_storage&) = delete;
    __op_storage& operator=(__op_storage&&) = delete;

    ~__op_storage() noexcept { destroy(); }

    template<class T, class... Args>
    T* emplace(Args&&... args) {
        return emplace_from<T>([&]() -> T {
            return T(std::forward<Args>(args)...);
        });
    }

    template<class T, class Factory>
    T* emplace_from(Factory&& factory) {
        destroy();
        constexpr bool fits_inline =
            sizeof(T) <= SmallSize && alignof(T) <= Align;

        if constexpr (fits_inline) {
            auto* ptr = ::new (static_cast<void*>(__buf)) T(std::forward<Factory>(factory)());
            __ptr = ptr;
            __dtor = [](void* p) noexcept { static_cast<T*>(p)->~T(); };
            return ptr;
        } else {
            auto* ptr = new T(std::forward<Factory>(factory)());
            __ptr = ptr;
            __dtor = [](void* p) noexcept { delete static_cast<T*>(p); };
            return ptr;
        }
    }

    void destroy() noexcept {
        if (__ptr == nullptr) return;
        __dtor(__ptr);
        __ptr = nullptr;
        __dtor = nullptr;
    }

    template<class T>
    T& get() noexcept {
        return *static_cast<T*>(__ptr);
    }

    bool has_value() const noexcept { return __ptr != nullptr; }

private:
    alignas(Align) unsigned char __buf[SmallSize]{};
    void* __ptr = nullptr;
    void (*__dtor)(void*) noexcept = nullptr;
};

} // namespace std::execution::__forge_detail
