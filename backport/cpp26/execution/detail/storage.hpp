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
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace std::execution::__forge_detail {

// Small-buffer-optimised (SBO) type-erased storage.
//
// Stores a value of type T in an inline buffer when
//   sizeof(T) <= SmallSize && alignof(T) <= Align
// and falls back to heap allocation otherwise.
//
// Intentionally non-copyable and non-movable: callers own the lifetime and
// must call destroy() before the storage object goes out of scope.
template <std::size_t SmallSize = 256,
          std::size_t Align     = alignof(std::max_align_t)>
class __sbo_storage {
public:
    __sbo_storage() noexcept = default;

    // Non-copyable, non-movable.
    __sbo_storage(const __sbo_storage&)            = delete;
    __sbo_storage(__sbo_storage&&)                 = delete;
    __sbo_storage& operator=(const __sbo_storage&) = delete;
    __sbo_storage& operator=(__sbo_storage&&)      = delete;

    ~__sbo_storage() noexcept { destroy(); }

    // Construct a T in-place, choosing SBO or heap based on size/alignment.
    // Returns a pointer to the constructed object.
    // Precondition: has_value() == false (i.e. destroy() was called or
    //               storage was never used).
    template <class T, class... Args>
    T* emplace(Args&&... args) {
        constexpr bool fits_sbo =
            (sizeof(T) <= SmallSize) && (alignof(T) <= Align);

        if constexpr (fits_sbo) {
            T* p = ::new (static_cast<void*>(__buf_)) T(std::forward<Args>(args)...);
            __ptr_     = p;
            __dtor_    = [](void* vp) noexcept {
                static_cast<T*>(vp)->~T();
            };
            __on_heap_ = false;
            return p;
        } else {
            T* p = new T(std::forward<Args>(args)...);
            __ptr_     = p;
            __dtor_    = [](void* vp) noexcept {
                delete static_cast<T*>(vp);
            };
            __on_heap_ = true;
            return p;
        }
    }

    // Destruct and (if heap-allocated) free the stored value.
    // Idempotent: safe to call when empty.
    void destroy() noexcept {
        if (__ptr_ == nullptr) return;
        __dtor_(__ptr_);
        __ptr_  = nullptr;
        __dtor_ = nullptr;
    }

    // Access the stored value as T*.  Returns nullptr if empty.
    template <class T>
    T* get() noexcept {
        return static_cast<T*>(__ptr_);
    }

    template <class T>
    const T* get() const noexcept {
        return static_cast<const T*>(__ptr_);
    }

    // True iff a value is currently stored.
    bool has_value() const noexcept { return __ptr_ != nullptr; }

private:
    alignas(Align) unsigned char __buf_[SmallSize]{};
    void*               __ptr_     = nullptr;
    void (*__dtor_)(void*) noexcept = nullptr;
    bool                __on_heap_ = false;
};

} // namespace std::execution::__forge_detail
