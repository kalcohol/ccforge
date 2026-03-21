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

namespace std {

// Stop token types are specified in namespace std in P2300R10.
// Guard with a future-facing feature test macro if/when it exists.
#if !defined(__cpp_lib_inplace_stop_token) || (__cpp_lib_inplace_stop_token < 202400L)

class inplace_stop_source;
class inplace_stop_token;
template<class Callback>
class inplace_stop_callback;

namespace __forge_stop_detail {

struct callback_base {
    using fn_t = void(callback_base*) noexcept;
    fn_t*          invoke_fn = nullptr;
    callback_base* next      = nullptr;
    callback_base** prev     = nullptr;
};

} // namespace __forge_stop_detail

class inplace_stop_source {
public:
    inplace_stop_source() noexcept = default;
    // Precondition: no inplace_stop_callback objects referencing this source
    // exist at the point of destruction.  See [stopsource.inplace.general].
    ~inplace_stop_source() noexcept = default;

    inplace_stop_source(const inplace_stop_source&) = delete;
    inplace_stop_source& operator=(const inplace_stop_source&) = delete;

    [[nodiscard]] inplace_stop_token get_token() noexcept;

    bool request_stop() noexcept {
        __forge_stop_detail::callback_base* head = nullptr;
        {
            std::lock_guard lk{mtx_};
            auto st = state_.load(std::memory_order_acquire);
            if ((st & kStopRequested) != 0) {
                return false;
            }
            state_.store(static_cast<std::uint8_t>(st | kStopRequested),
                         std::memory_order_release);

            // Detach the callback list under the lock, but do NOT invoke
            // callbacks here.  Invoking user callbacks while holding mtx_
            // would deadlock if the callback tries to register/remove
            // another callback.  See [stopsource.inplace.mem].
            head = callbacks_;
            callbacks_ = nullptr;
        }

        // Invoke callbacks outside the lock.
        while (head) {
            auto* next = head->next;
            head->next = nullptr;
            head->prev = nullptr;
            if (head->invoke_fn) {
                head->invoke_fn(head);
            }
            head = next;
        }
        return true;
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return (state_.load(std::memory_order_acquire) & kStopRequested) != 0;
    }

private:
    friend class inplace_stop_token;
    template<class Cb>
    friend class inplace_stop_callback;

    bool try_add_callback(__forge_stop_detail::callback_base* cb) noexcept {
        std::lock_guard lk{mtx_};
        if (stop_requested()) {
            return false;
        }

        cb->next = callbacks_;
        cb->prev = &callbacks_;
        if (callbacks_) {
            callbacks_->prev = &cb->next;
        }
        callbacks_ = cb;
        return true;
    }

    void remove_callback(__forge_stop_detail::callback_base* cb) noexcept {
        std::lock_guard lk{mtx_};
        if (!cb->prev) {
            return; // already removed/detached
        }

        if (cb->next) {
            cb->next->prev = cb->prev;
        }
        *cb->prev = cb->next;
        cb->next = nullptr;
        cb->prev = nullptr;
    }

    static constexpr std::uint8_t kStopRequested = 1;

    std::atomic<std::uint8_t> state_{0};
    std::mutex mtx_{};
    __forge_stop_detail::callback_base* callbacks_ = nullptr;
};

class inplace_stop_token {
public:
    inplace_stop_token() noexcept : source_(nullptr) {}

    [[nodiscard]] bool stop_requested() const noexcept {
        return source_ && source_->stop_requested();
    }
    [[nodiscard]] bool stop_possible() const noexcept { return source_ != nullptr; }

    void swap(inplace_stop_token& other) noexcept { std::swap(source_, other.source_); }
    friend void swap(inplace_stop_token& a, inplace_stop_token& b) noexcept { a.swap(b); }

    bool operator==(const inplace_stop_token&) const noexcept = default;

    // [stoptoken.inplace.general] — callback_type alias required by
    // stop_callback_for_t<inplace_stop_token, CB>.
    template<class CallbackFn>
    using callback_type = inplace_stop_callback<CallbackFn>;

private:
    friend class inplace_stop_source;
    template<class Cb>
    friend class inplace_stop_callback;

    explicit inplace_stop_token(inplace_stop_source* src) noexcept : source_(src) {}

    inplace_stop_source* source_;
};

inline inplace_stop_token inplace_stop_source::get_token() noexcept {
    return inplace_stop_token{this};
}

template<class Callback>
class inplace_stop_callback : __forge_stop_detail::callback_base {
public:
    using callback_type = Callback;

    template<class Cb>
        requires std::constructible_from<Callback, Cb>
    explicit inplace_stop_callback(inplace_stop_token token, Cb&& cb)
        noexcept(std::is_nothrow_constructible_v<Callback, Cb>)
        : callback_(std::forward<Cb>(cb))
        , source_(token.source_) {
        this->invoke_fn = [](std::__forge_stop_detail::callback_base* base) noexcept {
            auto* self = static_cast<inplace_stop_callback*>(base);
            self->callback_();
        };

        if (source_ && !source_->try_add_callback(this)) {
            // Already stopped: invoke immediately.
            callback_();
            source_ = nullptr;
        }
    }

    ~inplace_stop_callback() {
        if (source_) {
            source_->remove_callback(this);
        }
    }

    inplace_stop_callback(const inplace_stop_callback&) = delete;
    inplace_stop_callback& operator=(const inplace_stop_callback&) = delete;

private:
    [[no_unique_address]] Callback callback_;
    inplace_stop_source* source_;
};

template<class Cb>
inplace_stop_callback(inplace_stop_token, Cb) -> inplace_stop_callback<Cb>;

struct never_stop_token {
    [[nodiscard]] static constexpr bool stop_requested() noexcept { return false; }
    [[nodiscard]] static constexpr bool stop_possible() noexcept { return false; }
    bool operator==(const never_stop_token&) const noexcept = default;
};

// ── Stop token concepts — [stoptoken.concepts] ─────────────────────────

// stop_callback_for_t: alias for Token::callback_type<CallbackFn> if it
// exists, or Token::template callback_type<CallbackFn> otherwise.
// For inplace_stop_token the callback type is inplace_stop_callback<Fn>.
template<class Token, class CallbackFn>
using stop_callback_for_t = typename Token::template callback_type<CallbackFn>;

template<class Token>
concept stoppable_token =
    std::copy_constructible<Token> &&
    std::move_constructible<Token> &&
    std::is_nothrow_copy_constructible_v<Token> &&
    std::is_nothrow_move_constructible_v<Token> &&
    std::equality_comparable<Token> &&
    requires(const Token& token) {
        { token.stop_requested() } noexcept -> std::same_as<bool>;
        { token.stop_possible() } noexcept -> std::same_as<bool>;
    };

template<class Token, class CallbackFn>
concept stoppable_token_for =
    stoppable_token<Token> &&
    std::invocable<CallbackFn> &&
    requires { typename stop_callback_for_t<Token, CallbackFn>; } &&
    std::constructible_from<stop_callback_for_t<Token, CallbackFn>, Token, CallbackFn> &&
    std::constructible_from<stop_callback_for_t<Token, CallbackFn>, Token&, CallbackFn>;

template<class Token>
concept unstoppable_token =
    stoppable_token<Token> &&
    requires {
        requires std::bool_constant<(!Token::stop_possible())>::value;
    };

#endif // !__cpp_lib_inplace_stop_token

} // namespace std
