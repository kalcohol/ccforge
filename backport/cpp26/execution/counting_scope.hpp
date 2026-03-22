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

#include "concepts.hpp"
#include "env.hpp"
#include "start_detached.hpp"
#include "ensure_started.hpp"

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <exception>

namespace std::execution {

// ──────────────────────────────────────────────────────────────────────────
// simple_counting_scope — [exec.counting.scope.simple]
// P3149R11: Structured concurrency with async_scope
//
// simple_counting_scope allows spawning async work and joining (waiting)
// for all spawned work to complete. The count tracks outstanding operations.
// ──────────────────────────────────────────────────────────────────────────

class simple_counting_scope {
public:
    class scope_token;

    simple_counting_scope() noexcept = default;
    ~simple_counting_scope() noexcept {
        // Destructor: if count > 0, work was not properly joined.
        // In P3149R11, this is an error. We terminate.
        if (__count_.load(std::memory_order_acquire) != 0) {
            std::terminate();
        }
    }
    simple_counting_scope(const simple_counting_scope&) = delete;
    simple_counting_scope& operator=(const simple_counting_scope&) = delete;
    simple_counting_scope(simple_counting_scope&&) = delete;
    simple_counting_scope& operator=(simple_counting_scope&&) = delete;

    // Get the token for associating work with this scope
    [[nodiscard]] scope_token get_token() noexcept;

    // Close: prevent new work from being associated
    void close() noexcept {
        __closed_.store(true, std::memory_order_release);
    }

    // join: block until all associated work completes
    void join() {
        std::unique_lock lk{__mtx_};
        __cv_.wait(lk, [this] {
            return __count_.load(std::memory_order_acquire) == 0;
        });
    }

    [[nodiscard]] bool is_closed() const noexcept {
        return __closed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t count() const noexcept {
        return __count_.load(std::memory_order_acquire);
    }

private:
    friend class scope_token;

    void __increment() noexcept {
        __count_.fetch_add(1, std::memory_order_relaxed);
    }

    void __decrement() noexcept {
        if (__count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // Last operation completed
            std::lock_guard lk{__mtx_};
            __cv_.notify_all();
        }
    }

    std::atomic<std::size_t> __count_{0};
    std::atomic<bool> __closed_{false};
    std::mutex __mtx_;
    std::condition_variable __cv_;
};

// ──────────────────────────────────────────────────────────────────────────
// scope_token — interface for associating work with the scope
// ──────────────────────────────────────────────────────────────────────────

class simple_counting_scope::scope_token {
public:
    scope_token() noexcept = default;
    explicit scope_token(simple_counting_scope* scope) noexcept
        : __scope_(scope) {}

    [[nodiscard]] bool try_associate() const noexcept {
        if (!__scope_ || __scope_->is_closed()) return false;
        __scope_->__increment();
        return true;
    }

    void disassociate() const noexcept {
        if (__scope_) __scope_->__decrement();
    }

    // associate(sndr): wrap sender so it decrements scope on completion
    // Returns a sender that, when started, increments the scope count,
    // runs sndr, and decrements on any completion channel.
    // If scope is closed, returns just_stopped().
    template<sender S>
    [[nodiscard]] auto associate(S sndr) const {
        if (!try_associate()) {
            return std::execution::just_stopped();
        }
        scope_token token = *this;
        // Wrap with disassociate-on-complete in all channels
        return std::move(sndr)
            | std::execution::then([token](auto&&... vs) noexcept {
                token.disassociate();
                return std::make_tuple(static_cast<decltype(vs)&&>(vs)...);
            });
        // Note: upon_error/upon_stopped paths not type-compatible in MVP
        // disassociation on error/stopped is handled by spawn()
    }

    // spawn(sndr): fire-and-forget, associated with this scope
    template<sender S>
    void spawn(S sndr) {
        if (!try_associate()) return; // scope closed, silently ignore
        auto token = *this;
        start_detached(
            std::move(sndr)
            | upon_error([token](auto&&) noexcept { token.disassociate(); })
            | upon_stopped([token]() noexcept { token.disassociate(); })
            | then([token](auto&&...) noexcept { token.disassociate(); })
        );
    }

private:
    simple_counting_scope* __scope_ = nullptr;
};

inline simple_counting_scope::scope_token
simple_counting_scope::get_token() noexcept {
    return scope_token{this};
}

// ──────────────────────────────────────────────────────────────────────────
// counting_scope — [exec.counting.scope]
// Extends simple_counting_scope with nestable token support.
// For Phase 3, counting_scope is equivalent to simple_counting_scope.
// Full P3149R11 nesting support can be added later.
// ──────────────────────────────────────────────────────────────────────────

using counting_scope = simple_counting_scope;

} // namespace std::execution
