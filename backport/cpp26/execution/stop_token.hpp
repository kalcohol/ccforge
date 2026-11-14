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

#include <atomic>
#include <concepts>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <utility>
#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME) || defined(FORGE_EXECUTION_TEST_STOP_HOOKS)
#include <cstdio>
#include <cstdlib>
#endif

namespace std {

// Stop token types are specified in namespace std in P2300R10.
// Guard with a future-facing feature test macro if/when it exists.
#if !defined(__cpp_lib_inplace_stop_token) || (__cpp_lib_inplace_stop_token < 202400L)

class inplace_stop_source;
class inplace_stop_token;
template<class Callback>
class inplace_stop_callback;

namespace __forge_stop_detail {

// Names the place where the source lock is released or a wake decision is
// made. Production builds pass it to unlock() and ignore it; test builds
// report it to the thread-local observer below.
enum class stop_site : unsigned char {
    add,                  // try_add_callback linked the node (lock held)
    add_stopped,          // try_add_callback found the stop bit (lock held)
    request_lost,         // request_stop found the stop bit (lock held)
    request_invoke,       // request_stop selected a node to invoke (lock held)
    request_final,        // request_stop drained the list (lock held)
    remove_pending,       // remove_callback unlinked a pending node (lock held)
    remove_completed,     // remove_callback found the invocation done (lock held)
    remove_self,          // remove_callback ran on the requester (lock held)
    remove_await,         // remove_callback recorded awaited (lock held)
    remove_handoff,       // final handoff of a waiting destructor (lock held)
    lock_park,            // lock() is about to wait with kLockWaiters set
    lock_notify,          // unlock() released a word carrying kLockWaiters
    completion_published, // completion stored, notify not yet issued (lock held)
};

// Requester-owned record of one invocation. It lives on the requesting
// thread's stack and is read and written only under the source lock.
enum class invocation_status : unsigned char {
    running,                // no destructor has reached the executing node
    destroyed_on_requester, // destroyed on the requesting thread; node is gone
    awaited,                // a destructor on another thread waits for completion
};

#if defined(FORGE_EXECUTION_TEST_STOP_HOOKS)
// Each test thread installs its own observer before destroying a callback.
inline thread_local void* deregister_context = nullptr;
inline thread_local void (*deregister_wait_hook)(void*) noexcept = nullptr;
// Each test thread may also observe the lock protocol. Sites marked "lock
// held" run inside the source critical section: the observer may block there
// to keep the lock held, but must never call into the same source.
inline thread_local void* event_context = nullptr;
inline thread_local void (*event_hook)(void*, stop_site) noexcept = nullptr;
inline void test_event(stop_site site) noexcept {
    if (event_hook) event_hook(event_context, site);
}
#endif

struct callback_base {
    using fn_t = void(callback_base*) noexcept;
    fn_t*          invoke_fn = nullptr;
    callback_base* next      = nullptr;
    callback_base** prev     = nullptr;
    // invoke_fn is immutable after publication. Other non-atomic fields are
    // protected by the source lock. A nonnull prev means pending; a nonnull
    // status means selected for invocation and not yet completed, and points
    // to the requester's record for that invocation.
    invocation_status* status = nullptr;
    std::thread::id request_thread{};
    // Stored and notified only for a destructor that recorded awaited.
    alignas(sizeof(std::uint32_t)) std::atomic<std::uint32_t> completion{0};
};

} // namespace __forge_stop_detail

class inplace_stop_source {
public:
    constexpr inplace_stop_source() noexcept = default;
    // Precondition: no inplace_stop_callback objects referencing this source
    // exist and no request_stop() is executing at the point of destruction.
    // See [stopsource.inplace.general].
#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME)
    ~inplace_stop_source() noexcept {
        // Leave a tombstone in still-addressable storage. ASan continues to
        // diagnose freed storage; this also catches late teardown on the stack.
        // A thread parked in lock() violates the precondition; wake it so its
        // lock loop reports the violation instead of sleeping forever.
        if ((state_.exchange(kDestroyed, std::memory_order_release) & kLockWaiters) != 0) {
            state_.notify_all();
        }
    }
#else
    ~inplace_stop_source() noexcept = default;
#endif

    inplace_stop_source(const inplace_stop_source&) = delete;
    inplace_stop_source& operator=(const inplace_stop_source&) = delete;

    [[nodiscard]] constexpr inplace_stop_token get_token() const noexcept;
    [[nodiscard]] static constexpr bool stop_possible() noexcept { return true; }

    bool request_stop() noexcept {
        using __forge_stop_detail::invocation_status;
        using __forge_stop_detail::stop_site;
        if (stop_requested()) return false;
        auto state = lock();
        if ((state & kStopRequested) != 0) {
            unlock(stop_site::request_lost, state);
            return false;
        }
        // The release RMW publishes the request to acquire stop queries. It
        // must stay an RMW: a contender may set kLockWaiters concurrently.
        state_.fetch_or(kStopRequested, std::memory_order_release);
        state |= kStopRequested;
        while (callbacks_) {
            auto* node = callbacks_;
            unlink(node);
            invocation_status status = invocation_status::running;
            node->status = &status;
            node->request_thread = std::this_thread::get_id();
            unlock(stop_site::request_invoke, state);
            node->invoke_fn(node);
#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME)
            // Always revisit the source, even when the callback destroyed itself.
            // Volatile preserves the ASan negative control in optimized tests.
            const volatile auto source_state = stop_requested();
            (void)source_state;
#endif
            state = lock();
            // A destructor on this thread only wrote the stack record. Never
            // dereference node again in that case, including for the strict
            // lifetime probe.
            if (status != invocation_status::destroyed_on_requester) {
#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME)
                const volatile auto* live_node = node;
                const auto node_function = live_node->invoke_fn;
                (void)node_function;
#endif
                node->status = nullptr;
                // Only a destructor that recorded awaited under this lock waits
                // for completion. One arriving later sees status == nullptr and
                // returns without reading completion.
                if (status == invocation_status::awaited) {
                    // Publish callback side effects to the waiting destructor.
                    // seq_cst closes the store-buffer pattern when notify checks
                    // a waiter count with a plain load and could miss the waiter.
                    // Notify while locked: the waiter must reacquire this lock
                    // before freeing the atomic's storage.
                    node->completion.store(1, std::memory_order_seq_cst);
#if defined(FORGE_EXECUTION_TEST_STOP_HOOKS)
                    __forge_stop_detail::test_event(stop_site::completion_published);
#endif
                    node->completion.notify_all();
#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME)
                    // The waiter cannot reclaim the node before this thread
                    // unlocks; without the final handoff this read is a use
                    // after free that ASan reports.
                    const auto notified_function = live_node->invoke_fn;
                    (void)notified_function;
#endif
                }
            }
        }
        unlock(stop_site::request_final, state);
        return true;
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        const auto state = state_.load(std::memory_order_acquire);
#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME)
        check_lifetime(state);
#endif
        return (state & kStopRequested) != 0;
    }

private:
    friend class inplace_stop_token;
    template<class Cb>
    friend class inplace_stop_callback;

    bool try_add_callback(__forge_stop_detail::callback_base* cb) const noexcept {
        using __forge_stop_detail::stop_site;
        const auto state = lock();
        if ((state & kStopRequested) != 0) {
            unlock(stop_site::add_stopped, state);
            return false;
        }

        cb->next = callbacks_;
        cb->prev = &callbacks_;
        if (callbacks_) {
            callbacks_->prev = &cb->next;
        }
        callbacks_ = cb;
        unlock(stop_site::add, state);
        return true;
    }

    void remove_callback(__forge_stop_detail::callback_base* cb) const noexcept {
        using __forge_stop_detail::invocation_status;
        using __forge_stop_detail::stop_site;
        const auto state = lock();
        if (cb->prev) {
            unlink(cb);
            unlock(stop_site::remove_pending, state);
            return;
        }
        if (!cb->status) {
            unlock(stop_site::remove_completed, state);
            return; // invocation and its final node accesses have completed
        }
        if (cb->request_thread == std::this_thread::get_id()) {
            *cb->status = invocation_status::destroyed_on_requester;
            unlock(stop_site::remove_self, state);
            return; // the requester will use only its stack record
        }
        // Record the wait in the same critical section that observed the
        // invocation unfinished. The requester reads the record under this
        // lock, so it stores and notifies completion exactly when awaited.
        *cb->status = invocation_status::awaited;
        unlock(stop_site::remove_await, state);
#if defined(FORGE_EXECUTION_TEST_STOP_HOOKS)
        if (__forge_stop_detail::deregister_wait_hook) {
            __forge_stop_detail::deregister_wait_hook(
                __forge_stop_detail::deregister_context);
        }
#endif
        // Only this callback's invocation can delay destruction. Acquire makes
        // invocation side effects visible; wait parks rather than busy-spinning.
        while (cb->completion.load(std::memory_order_acquire) == 0) {
            cb->completion.wait(0, std::memory_order_acquire);
        }
        // completion may become visible before notify_all finishes. Exclude the
        // requester's last node access before allowing storage reclamation.
        unlock(stop_site::remove_handoff, lock());
    }

    void unlink(__forge_stop_detail::callback_base* cb) const noexcept {
        if (cb->next) {
            cb->next->prev = cb->prev;
        }
        *cb->prev = cb->next;
        cb->next = nullptr;
        cb->prev = nullptr;
    }

    // Lock word invariants:
    // - kLockWaiters is set only while kLocked is set, by a contender's CAS on
    //   a locked word. Only unlock() clears either bit, and it clears both.
    // - While a thread holds the lock, the only write to state_ by any other
    //   thread is that contender CAS. A new writer of state_ must keep this
    //   true, or unlock() would silently discard its write.
    // Successful acquire pairs with the preceding owner's unlock. Failed CAS,
    // polling, the waiter-bit CAS and wait only select a retry, so they need
    // no ordering.
    std::uint32_t lock() const noexcept {
        auto state = state_.load(std::memory_order_relaxed);
        for (;;) {
#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME)
            check_lifetime(state);
#endif
            if ((state & kLocked) == 0) {
                // An unlocked word never carries kLockWaiters.
                if (state_.compare_exchange_weak(state, state | kLocked,
                        std::memory_order_acquire, std::memory_order_relaxed)) {
                    return state;
                }
            } else {
                state = wait_for_unlock(state);
            }
        }
    }

    // Precondition: state was loaded from state_ and has kLocked set.
    // Returns a freshly loaded word for lock() to examine again.
    std::uint32_t wait_for_unlock(std::uint32_t state) const noexcept {
        // Holds last a few stores. Re-read before publishing the waiter bit,
        // which would take the owner's cache line and make its unlock notify.
        for (int attempt = 0; attempt != kLockBackoffYields; ++attempt) {
            std::this_thread::yield();
            state = state_.load(std::memory_order_relaxed);
            if ((state & kLocked) == 0) return state;
        }
        if ((state & kLockWaiters) == 0) {
            // Succeeds only on the locked word just observed, so the unlock
            // that ends this hold reads the bit. Relaxed: nothing is published.
            if (!state_.compare_exchange_weak(state, state | kLockWaiters,
                    std::memory_order_relaxed, std::memory_order_relaxed)) {
                return state; // examine again; never wait on an unarmed word
            }
            state |= kLockWaiters;
        }
#if defined(FORGE_EXECUTION_TEST_STOP_HOOKS)
        __forge_stop_detail::test_event(__forge_stop_detail::stop_site::lock_park);
#endif
        // state carries kLocked | kLockWaiters: the unlock ending this hold
        // reads the bit and notifies after releasing.
        state_.wait(state, std::memory_order_relaxed);
        return state_.load(std::memory_order_relaxed);
    }

    // known: the word as this critical section leaves it, without kLocked and
    // kLockWaiters: the value lock() returned, plus kStopRequested if this
    // section set it.
    void unlock([[maybe_unused]] __forge_stop_detail::stop_site site,
                std::uint32_t known) const noexcept {
#if defined(FORGE_EXECUTION_TEST_STOP_HOOKS)
        __forge_stop_detail::test_event(site);
#endif
        // By the invariants above the word is known | kLocked, possibly with
        // kLockWaiters, so this exchange is fetch_and(~(kLocked | kLockWaiters))
        // without a load or CAS loop, and its result decides the wake exactly.
        // seq_cst rather than release: it also orders this write before the
        // waiter check inside the library's notify. A wake missed there would
        // be permanent, because no later unlock sees the cleared bit.
        const auto previous = state_.exchange(known, std::memory_order_seq_cst);
#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME)
        check_lifetime(previous); // the source was destroyed during the hold
#endif
#if defined(FORGE_EXECUTION_TEST_STOP_HOOKS) || defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME)
        if ((previous & ~kLockWaiters) != (known | kLocked)) {
            std::fputs("inplace stop source lock word changed while held\n", stderr);
            std::abort();
        }
#endif
        if ((previous & kLockWaiters) != 0) {
#if defined(FORGE_EXECUTION_TEST_STOP_HOOKS)
            __forge_stop_detail::test_event(__forge_stop_detail::stop_site::lock_notify);
#endif
            // After the release, so woken waiters can take the lock. Wake all
            // of them: the bit is now clear for every waiter, and those that
            // lose the race publish it again before parking.
            state_.notify_all();
        }
    }

    static constexpr std::uint32_t kStopRequested = 1;
    static constexpr std::uint32_t kLocked = 2;
    static constexpr std::uint32_t kLockWaiters = 4;
    // Read-only yields before a contender publishes kLockWaiters.
    static constexpr int kLockBackoffYields = 4;
#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME)
    static constexpr std::uint32_t kDestroyed = 0x80000000u;
    static void check_lifetime(std::uint32_t state) noexcept {
        if ((state & kDestroyed) != 0) {
            std::fputs("inplace stop source accessed after destruction\n", stderr);
            std::abort();
        }
    }
#endif

    // Use naturally aligned words for atomic wait on Windows and AppleClang.
    alignas(sizeof(std::uint32_t)) mutable std::atomic<std::uint32_t> state_{0};
    mutable __forge_stop_detail::callback_base* callbacks_ = nullptr;
};

class inplace_stop_token {
public:
    constexpr inplace_stop_token() noexcept : source_(nullptr) {}

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

    explicit constexpr inplace_stop_token(const inplace_stop_source* src) noexcept
        : source_(src) {}

    const inplace_stop_source* source_;
};

inline constexpr inplace_stop_token inplace_stop_source::get_token() const noexcept {
    return inplace_stop_token{this};
}

template<class Callback>
class inplace_stop_callback : __forge_stop_detail::callback_base {
    static_assert(std::invocable<Callback> && std::destructible<Callback>);
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
            std::forward<Callback>(self->callback_)();
        };
        if (source_ && !source_->try_add_callback(this)) {
            // Immediate invocations are not associated. Clear before invoking
            // to allow self-destruction; the noexcept thunk enforces terminate
            // on invocation exceptions even if callback construction can throw.
            source_ = nullptr;
            this->invoke_fn(this);
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
    const inplace_stop_source* source_;
};

template<class Cb>
inplace_stop_callback(inplace_stop_token, Cb) -> inplace_stop_callback<Cb>;

struct never_stop_token {
private:
    struct __callback_type {
        template<class Initializer>
        explicit __callback_type(never_stop_token, Initializer&&) noexcept {}
    };

public:
    template<class>
    using callback_type = __callback_type;

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

namespace __forge_stop_detail {

struct stoppable_token_probe_callback {
    void operator()() const noexcept {}
};

} // namespace __forge_stop_detail

template<class Token>
concept stoppable_token =
    std::copy_constructible<Token> &&
    std::move_constructible<Token> &&
    std::is_nothrow_copy_constructible_v<Token> &&
    std::is_nothrow_move_constructible_v<Token> &&
    std::equality_comparable<Token> &&
    requires {
        typename Token::template callback_type<
            __forge_stop_detail::stoppable_token_probe_callback>;
    } &&
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
