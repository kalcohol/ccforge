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
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <variant>

namespace forge {

// ──────────────────────────────────────────────────────────────────────────
// any_sender_of<CompletionSignatures>
//
// Type-erased sender extension (forge:: namespace, not std::execution).
// Stores any sender with SBO (64B) + heap fallback. The sender concept
// and completion signatures are preserved.
//
// LIMITATION: connect() requires the receiver type to be known. Use
// any_sender_of::sync_wait_result<ValueTuple>() for the common case,
// or extract<SenderType>() to recover the original type.
// ──────────────────────────────────────────────────────────────────────────

// Helper: the value tuple type of a given CompletionSignatures.
template<class CS>
using value_tuple_of_t =
    std::execution::__forge_meta::__single_value_tuple_t<CS>;

template<class CompletionSignatures>
class any_sender_of {
    static constexpr std::size_t kSBOSize  = 64;
    static constexpr std::size_t kSBOAlign = alignof(std::max_align_t);

    struct __vtable {
        void (*destroy)(void* p) noexcept;
        void (*move_to)(void* src, void* dst) noexcept;
        // sync_wait path: run the sender, get the value tuple
        using value_tuple_t = value_tuple_of_t<CompletionSignatures>;
        std::optional<value_tuple_t> (*do_sync_wait)(void* p);
    };

    template<class S>
    static const __vtable* __make_vtable() {
        using VT = value_tuple_of_t<CompletionSignatures>;
        static const __vtable vt{
            .destroy  = [](void* p) noexcept { static_cast<S*>(p)->~S(); },
            .move_to  = [](void* src, void* dst) noexcept {
                ::new(dst) S(std::move(*static_cast<S*>(src)));
            },
            .do_sync_wait = [](void* p) -> std::optional<VT> {
                return std::execution::sync_wait(*static_cast<S*>(p));
            },
        };
        return &vt;
    }

    alignas(kSBOAlign) unsigned char __buf[kSBOSize]{};
    bool __on_heap = false;
    void* __ptr = nullptr;
    const __vtable* __vt = nullptr;

public:
    using sender_concept = std::execution::sender_t;
    using value_tuple_t  = value_tuple_of_t<CompletionSignatures>;

    any_sender_of() = default;

    template<std::execution::sender S>
        requires (!std::is_same_v<std::remove_cvref_t<S>, any_sender_of>)
    any_sender_of(S&& sndr) {
        using D = std::remove_cvref_t<S>;
        if constexpr (sizeof(D) <= kSBOSize && alignof(D) <= kSBOAlign) {
            __ptr = ::new(static_cast<void*>(__buf)) D(std::forward<S>(sndr));
            __on_heap = false;
        } else {
            __ptr = new D(std::forward<S>(sndr));
            __on_heap = true;
        }
        __vt = __make_vtable<D>();
    }

    any_sender_of(any_sender_of&& other) noexcept {
        if (!other.__ptr) return;
        if (!other.__on_heap) {
            other.__vt->move_to(other.__ptr, static_cast<void*>(__buf));
            __ptr = static_cast<void*>(__buf);
            __on_heap = false;
        } else {
            __ptr = other.__ptr;
            __on_heap = true;
        }
        __vt = other.__vt;
        other.__ptr = nullptr;
    }

    any_sender_of& operator=(any_sender_of&& other) noexcept {
        if (this == &other) return *this;
        reset();
        ::new(this) any_sender_of(std::move(other));
        return *this;
    }

    any_sender_of(const any_sender_of&) = delete;
    any_sender_of& operator=(const any_sender_of&) = delete;

    ~any_sender_of() { reset(); }

    void reset() noexcept {
        if (__ptr && __vt) {
            __vt->destroy(__ptr);
            if (__on_heap) ::operator delete(__ptr);
        }
        __ptr = nullptr;
        __vt  = nullptr;
    }

    explicit operator bool() const noexcept { return __ptr != nullptr; }

    // Convenience: run sync_wait on the stored sender
    [[nodiscard]] std::optional<value_tuple_t> sync_wait() {
        if (!__ptr) throw std::runtime_error("any_sender_of: empty");
        return __vt->do_sync_wait(__ptr);
    }

    // forge:: extension sender interface
    friend CompletionSignatures tag_invoke(
        std::execution::get_completion_signatures_t,
        const any_sender_of&, auto) noexcept {
        return {};
    }

    friend std::execution::empty_env tag_invoke(
        std::execution::get_env_t, const any_sender_of&) noexcept {
        return {};
    }
};

} // namespace forge
