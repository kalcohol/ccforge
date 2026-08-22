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

#include "any_stop_token.hpp"

#include <execution>
#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace forge {

template<class Sig, class CompletionSignatures>
struct __any_receiver_contains_signature : std::false_type {};

template<class Sig, class... Sigs>
struct __any_receiver_contains_signature<
    Sig,
    std::execution::completion_signatures<Sigs...>>
    : std::bool_constant<(std::is_same_v<Sig, Sigs> || ...)> {};

template<class Sig, class CompletionSignatures>
inline constexpr bool __any_receiver_contains_signature_v =
    __any_receiver_contains_signature<Sig, CompletionSignatures>::value;

template<class CompletionSignatures>
struct __any_receiver_value_shape {
    using tuple_type = std::tuple<>;
    static constexpr bool present = false;
};

template<class... Vs, class... Rest>
struct __any_receiver_value_shape<
    std::execution::completion_signatures<
        std::execution::set_value_t(Vs...),
        Rest...>> {
    using tuple_type = std::tuple<Vs&&...>;
    static constexpr bool present = true;
};

template<class Other, class... Rest>
struct __any_receiver_value_shape<
    std::execution::completion_signatures<Other, Rest...>>
    : __any_receiver_value_shape<
          std::execution::completion_signatures<Rest...>> {};

// any_receiver_of<CS> — narrow receiver erasure with a fixed completion vtable.

template<class CompletionSignatures>
class any_receiver_of {
    using cs_t = CompletionSignatures;
    using value_shape_t = __any_receiver_value_shape<cs_t>;
    using value_tuple_t = typename value_shape_t::tuple_type;
    static constexpr bool __has_value_completion =
        value_shape_t::present;

    struct __vtable {
        void (*complete_value)(void*, void*) noexcept;
        void (*complete_error)(void*, std::exception_ptr) noexcept;
        void (*complete_stopped)(void*) noexcept;
        void (*destroy)(void*) noexcept;
        void (*destroy_heap)(void*) noexcept;
        void (*move_to)(void*, void*) noexcept;
    };

    template<class R>
    static const __vtable* __make_vtable() {
        static const __vtable vt{
            .complete_value = [](void* p, void* tuple) noexcept {
                if constexpr (__has_value_completion) {
                    auto& values = *static_cast<value_tuple_t*>(tuple);
                    std::apply([&](auto&&... vs) noexcept {
                        std::execution::set_value(
                            std::move(*static_cast<R*>(p)),
                            static_cast<decltype(vs)&&>(vs)...);
                    }, std::move(values));
                } else {
                    std::terminate();
                }
            },
            .complete_error = [](void* p, std::exception_ptr ep) noexcept {
                if constexpr (__any_receiver_contains_signature_v<
                                  std::execution::set_error_t(std::exception_ptr),
                                  cs_t>) {
                    std::execution::set_error(
                        std::move(*static_cast<R*>(p)),
                        std::move(ep));
                } else {
                    std::terminate();
                }
            },
            .complete_stopped = [](void* p) noexcept {
                if constexpr (__any_receiver_contains_signature_v<
                                  std::execution::set_stopped_t(),
                                  cs_t>) {
                    std::execution::set_stopped(std::move(*static_cast<R*>(p)));
                } else {
                    std::terminate();
                }
            },
            .destroy = [](void* p) noexcept {
                static_cast<R*>(p)->~R();
            },
            .destroy_heap = [](void* p) noexcept {
                delete static_cast<R*>(p);
            },
            .move_to = [](void* src, void* dst) noexcept {
                ::new(dst) R(std::move(*static_cast<R*>(src)));
            },
        };
        return &vt;
    }

    template<class R>
    static auto __make_stop_token(const R& receiver) -> any_stop_token {
        return any_stop_token{
            std::execution::get_stop_token(std::execution::get_env(receiver))};
    }

    struct __env {
        any_stop_token token;

        auto query(std::execution::get_stop_token_t) const noexcept
            -> any_stop_token {
            return token;
        }

        friend auto tag_invoke(
            std::execution::get_stop_token_t,
            const __env& self) noexcept -> any_stop_token {
            return self.query(std::execution::get_stop_token);
        }
    };

    static constexpr std::size_t kSBOSize = 64;
    alignas(std::max_align_t) unsigned char __buf[kSBOSize]{};
    bool __on_heap = false;
    void* __ptr = nullptr;
    const __vtable* __vt = nullptr;
    any_stop_token __stop_token{};

public:
    using receiver_concept = std::execution::receiver_t;

    any_receiver_of() = default;

    // Self-exclusion guard must precede the receiver<R> check: receiver requires
    // is_nothrow_move_constructible_v, which for any_receiver_of re-enters this
    // constructor's constraint and recurses. Left-to-right short-circuiting on the
    // cheap !is_same_v guard prunes the self type before receiver<R> is evaluated.
    // (libstdc++ tolerated it; libc++/clang-19 diagnoses it as a hard error.)
    template<class R>
        requires (!std::is_same_v<std::remove_cvref_t<R>, any_receiver_of>)
              && std::execution::receiver_of<std::remove_cvref_t<R>, cs_t>
    any_receiver_of(R&& r) {
        using D = std::remove_cvref_t<R>;
        if constexpr (sizeof(D) <= kSBOSize && alignof(D) <= alignof(std::max_align_t)) {
            __ptr = ::new(static_cast<void*>(__buf)) D(std::forward<R>(r));
            __on_heap = false;
        } else {
            __ptr = new D(std::forward<R>(r));
            __on_heap = true;
        }
        try {
            __stop_token = __make_stop_token(*static_cast<D*>(__ptr));
        } catch (...) {
            if (__on_heap) {
                delete static_cast<D*>(__ptr);
            } else {
                static_cast<D*>(__ptr)->~D();
            }
            __ptr = nullptr;
            __on_heap = false;
            throw;
        }
        __vt = __make_vtable<D>();
    }

    any_receiver_of(any_receiver_of&& o) noexcept {
        if (!o.__ptr) return;
        __vt = o.__vt;
        __stop_token = std::move(o.__stop_token);
        if (!o.__on_heap) {
            __vt->move_to(o.__ptr, static_cast<void*>(__buf));
            __vt->destroy(o.__ptr);
            __ptr = static_cast<void*>(__buf);
            __on_heap = false;
        } else {
            __ptr = o.__ptr;
            __on_heap = true;
        }
        o.__ptr = nullptr;
        o.__vt = nullptr;
        o.__on_heap = false;
        o.__stop_token = {};
    }

    any_receiver_of& operator=(any_receiver_of&&) = delete;
    any_receiver_of(const any_receiver_of&) = delete;

    ~any_receiver_of() {
        if (__ptr && __vt) {
            if (__on_heap) {
                __vt->destroy_heap(__ptr);
            } else {
                __vt->destroy(__ptr);
            }
        }
    }

    explicit operator bool() const noexcept { return __ptr != nullptr; }

    template<class... Vs>
        requires __has_value_completion &&
                 std::is_same_v<
                     std::tuple<Vs&&...>,
                     value_tuple_t>
    void set_value(Vs&&... vs) && noexcept {
        if (__ptr && __vt) {
            auto values = value_tuple_t{static_cast<Vs&&>(vs)...};
            __vt->complete_value(__ptr, &values);
        }
    }

    template<class E>
        requires __any_receiver_contains_signature_v<
            std::execution::set_error_t(std::exception_ptr),
            cs_t>
    void set_error(E&& e) && noexcept {
        if (__ptr && __vt) {
            std::exception_ptr ep;
            if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>)
                ep = static_cast<E&&>(e);
            else
                ep = std::make_exception_ptr(static_cast<E&&>(e));
            __vt->complete_error(__ptr, std::move(ep));
        }
    }

    void set_stopped() && noexcept
        requires __any_receiver_contains_signature_v<
            std::execution::set_stopped_t(),
            cs_t>
    {
        if (__ptr && __vt) __vt->complete_stopped(__ptr);
    }

    auto get_env() const noexcept -> __env {
        return {__stop_token};
    }
};

} // namespace forge
