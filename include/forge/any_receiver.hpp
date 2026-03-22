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

#include <execution>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>

namespace forge {

// any_receiver_of<CS> — type-erased receiver with virtual dispatch (no-template virtual fix)
// Uses std::function for the complete callbacks since template virtual is not allowed.

template<class CompletionSignatures>
class any_receiver_of {
    using cs_t = CompletionSignatures;
    using value_tuple_t = std::execution::__forge_meta::__single_value_tuple_t<cs_t>;

    struct __vtable {
        void (*complete_value)(void*, value_tuple_t) noexcept;
        void (*complete_error)(void*, std::exception_ptr) noexcept;
        void (*complete_stopped)(void*) noexcept;
        void (*destroy)(void*) noexcept;
    };

    template<class R>
    static const __vtable* __make_vtable() {
        static const __vtable vt{
            .complete_value = [](void* p, value_tuple_t v) noexcept {
                std::apply([&](auto&&... vs) {
                    std::execution::set_value(
                        std::move(*static_cast<R*>(p)),
                        std::move(vs)...);
                }, std::move(v));
            },
            .complete_error = [](void* p, std::exception_ptr ep) noexcept {
                std::execution::set_error(std::move(*static_cast<R*>(p)), std::move(ep));
            },
            .complete_stopped = [](void* p) noexcept {
                std::execution::set_stopped(std::move(*static_cast<R*>(p)));
            },
            .destroy = [](void* p) noexcept {
                static_cast<R*>(p)->~R();
            },
        };
        return &vt;
    }

    static constexpr std::size_t kSBOSize = 64;
    alignas(std::max_align_t) unsigned char __buf[kSBOSize]{};
    bool __on_heap = false;
    void* __ptr = nullptr;
    const __vtable* __vt = nullptr;

public:
    using receiver_concept = std::execution::receiver_t;

    any_receiver_of() = default;

    template<std::execution::receiver R>
        requires (!std::is_same_v<std::remove_cvref_t<R>, any_receiver_of>)
    any_receiver_of(R&& r) {
        using D = std::remove_cvref_t<R>;
        if constexpr (sizeof(D) <= kSBOSize && alignof(D) <= alignof(std::max_align_t)) {
            __ptr = ::new(static_cast<void*>(__buf)) D(std::forward<R>(r));
            __on_heap = false;
        } else {
            __ptr = new D(std::forward<R>(r));
            __on_heap = true;
        }
        __vt = __make_vtable<D>();
    }

    any_receiver_of(any_receiver_of&& o) noexcept {
        if (!o.__ptr) return;
        if (!o.__on_heap) {
            std::memcpy(__buf, o.__buf, kSBOSize);
            __ptr = static_cast<void*>(__buf);
            __on_heap = false;
        } else {
            __ptr = o.__ptr;
            __on_heap = true;
        }
        __vt = o.__vt;
        o.__ptr = nullptr;
    }

    any_receiver_of& operator=(any_receiver_of&&) = delete;
    any_receiver_of(const any_receiver_of&) = delete;

    ~any_receiver_of() {
        if (__ptr && __vt) {
            __vt->destroy(__ptr);
            if (__on_heap) ::operator delete(__ptr);
        }
    }

    explicit operator bool() const noexcept { return __ptr != nullptr; }

    template<class... Vs>
    friend void tag_invoke(std::execution::set_value_t,
                           any_receiver_of&& self, Vs&&... vs) noexcept {
        if (self.__ptr && self.__vt)
            self.__vt->complete_value(self.__ptr,
                value_tuple_t{static_cast<Vs&&>(vs)...});
    }

    template<class E>
    friend void tag_invoke(std::execution::set_error_t,
                           any_receiver_of&& self, E&& e) noexcept {
        if (self.__ptr && self.__vt) {
            std::exception_ptr ep;
            if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>)
                ep = static_cast<E&&>(e);
            else
                ep = std::make_exception_ptr(static_cast<E&&>(e));
            self.__vt->complete_error(self.__ptr, std::move(ep));
        }
    }

    friend void tag_invoke(std::execution::set_stopped_t,
                           any_receiver_of&& self) noexcept {
        if (self.__ptr && self.__vt) self.__vt->complete_stopped(self.__ptr);
    }

    friend std::execution::empty_env tag_invoke(
        std::execution::get_env_t, const any_receiver_of&) noexcept {
        return {};
    }
};

} // namespace forge
