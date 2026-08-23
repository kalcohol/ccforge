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

#include <forge/io/detail/coro_completion_meta.hpp>
#include <forge/io/env.hpp>

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <execution>
#include <memory>
#include <memory_resource>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>

namespace forge::io {

template<class T>
class io_task;

namespace __coro_detail {

template<class T>
class task_awaitable;

} // namespace __coro_detail

template<class Awaitable>
concept io_awaitable =
    requires(Awaitable& awaitable,
             std::coroutine_handle<> continuation,
             const io_env* env) {
        { awaitable.await_ready() } -> std::convertible_to<bool>;
        { awaitable.await_suspend(continuation, env) }
            -> __coro_detail::await_suspend_result;
        awaitable.await_resume();
    };

namespace __coro_detail {

// Coroutine frames allocated through promise_base reserve a pointer-sized
// trailer behind the frame bytes. The trailer records which
// std::pmr::memory_resource owns the frame (null means global operator new),
// so the sized operator delete can route the deallocation without any side
// table. The trailer is written and read with memcpy because the compiler
// only guarantees raw storage there.
inline constexpr std::size_t frame_allocation_alignment =
    __STDCPP_DEFAULT_NEW_ALIGNMENT__;

[[nodiscard]] constexpr auto frame_trailer_offset(std::size_t size) noexcept
    -> std::size_t {
    constexpr std::size_t align = alignof(std::pmr::memory_resource*);
    return ((size + align - 1U) / align) * align;
}

[[nodiscard]] constexpr auto frame_allocation_size(std::size_t size) noexcept
    -> std::size_t {
    return frame_trailer_offset(size) + sizeof(std::pmr::memory_resource*);
}

inline auto stash_frame_resource(
    void* frame,
    std::size_t size,
    std::pmr::memory_resource* memory) noexcept -> void {
    std::memcpy(
        static_cast<unsigned char*>(frame) + frame_trailer_offset(size),
        &memory,
        sizeof(memory));
}

[[nodiscard]] inline auto stashed_frame_resource(
    void* frame,
    std::size_t size) noexcept -> std::pmr::memory_resource* {
    std::pmr::memory_resource* memory = nullptr;
    std::memcpy(
        &memory,
        static_cast<unsigned char*>(frame) + frame_trailer_offset(size),
        sizeof(memory));
    return memory;
}

// Completion-slot states shared between the sender bridge awaitable and
// the abandonment arbitration in abandon_task_chain.
enum class bridge_state {
    inactive,
    starting,
    suspended,
    completed,
    // Claimed by an abandoning destruction: a late delivery must not
    // resume the dying coroutine.
    abandoned
};

// Downward links for iterative abandonment. Destroying a suspended
// co_await io_task chain through nested task_awaitable destructors costs
// one set of native stack frames per level and overflows for deep chains,
// so every frame records its awaited child and how to detach the owning
// awaitable; destroy_frame_chain then walks the chain in a loop.
struct frame_chain_link {
    using bridge_token = std::uint64_t;

    static constexpr bridge_token bridge_state_mask = 0x7U;
    static constexpr std::uint32_t deferred_destroy_bit = 0x8000'0000U;
    static constexpr std::uint32_t resume_count_mask = ~deferred_destroy_bit;

    std::coroutine_handle<> child_frame{};
    frame_chain_link* child_link = nullptr;
    void* owned_task = nullptr;
    void (*release_owned)(void*) noexcept = nullptr;

    // Abandonment arbitration state. root_link lets every frame reach the
    // root in O(1) (propagated when a child is linked), so the leaf's
    // sender bridge can publish its completion slot where the abandoning
    // destructor looks without walking mutating links. started/finalized
    // bracket the window in which a cross-thread resume can exist.
    // resume_state combines the in-flight resume count with the deferred
    // destruction bit, so the last resume tail cannot miss a concurrent
    // same-stack abandonment. active_bridge is a generation-tagged slot
    // stored in the root itself; no arbitration thread ever dereferences an
    // awaitable-owned atomic after that awaitable's lifetime ends.
    frame_chain_link* root_link = this;
    std::coroutine_handle<> self_frame{};
    std::atomic<bool> started{false};
    std::atomic<bool> finalized{false};
    std::atomic<std::uint32_t> resume_state{0};
    std::atomic<bridge_token> active_bridge{0};
    std::atomic<bridge_token> next_bridge_generation{0};

    [[nodiscard]] static constexpr auto bridge_state_of(
        bridge_token token) noexcept -> bridge_state {
        return static_cast<bridge_state>(token & bridge_state_mask);
    }

    [[nodiscard]] static constexpr auto same_bridge(
        bridge_token left,
        bridge_token right) noexcept -> bool {
        return left != 0 && right != 0 &&
            (left & ~bridge_state_mask) == (right & ~bridge_state_mask);
    }

    [[nodiscard]] static constexpr auto with_bridge_state(
        bridge_token token,
        bridge_state state) noexcept -> bridge_token {
        return (token & ~bridge_state_mask) |
            static_cast<bridge_token>(state);
    }

    [[nodiscard]] auto publish_bridge() noexcept -> bridge_token {
        const bridge_token generation =
            next_bridge_generation.fetch_add(1, std::memory_order_relaxed) + 1;
        const bridge_token token =
            (generation << 3U) |
            static_cast<bridge_token>(bridge_state::starting);
        if (active_bridge.exchange(token, std::memory_order_acq_rel) != 0) {
            std::terminate();
        }
        return token;
    }

    [[nodiscard]] auto transition_bridge(
        bridge_token token,
        bridge_state desired) noexcept -> bridge_state {
        bridge_token current = active_bridge.load(std::memory_order_acquire);
        while (same_bridge(current, token)) {
            const bridge_token next = with_bridge_state(current, desired);
            if (active_bridge.compare_exchange_weak(
                    current,
                    next,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return bridge_state_of(current);
            }
        }
        return bridge_state::inactive;
    }

    auto clear_bridge(bridge_token token) noexcept -> void {
        bridge_token current = active_bridge.load(std::memory_order_acquire);
        while (same_bridge(current, token) &&
               !active_bridge.compare_exchange_weak(
                   current,
                   0,
                   std::memory_order_acq_rel,
                   std::memory_order_acquire)) {}
    }

    auto reset_child() noexcept -> void {
        child_frame = {};
        child_link = nullptr;
        owned_task = nullptr;
        release_owned = nullptr;
    }
};

// Destroys a suspended frame chain iteratively from the outermost frame.
// Before each frame dies, the task_awaitable inside it is detached from
// the child frame via release_owned, so the awaitable destructor that
// runs during frame.destroy() does not recurse into the child; the child
// is destroyed by the next loop iteration instead.
inline auto destroy_frame_chain(
    std::coroutine_handle<> frame,
    frame_chain_link* link) noexcept -> void {
    while (frame) {
        std::coroutine_handle<> child{};
        frame_chain_link* child_link = nullptr;
        if (link != nullptr && link->child_frame) {
            child = link->child_frame;
            child_link = link->child_link;
            link->release_owned(link->owned_task);
        }
        frame.destroy();
        frame = child;
        link = child_link;
    }
}

// Bounded yields, then sleep; the counter stops once saturated so it
// cannot overflow.
inline auto abandon_backoff_step(int& spins) noexcept -> void {
    if (spins < 64) {
        ++spins;
        std::this_thread::yield();
    } else {
        std::this_thread::sleep_for(std::chrono::microseconds{100});
    }
}

struct resume_scope {
    explicit resume_scope(frame_chain_link* root) noexcept
        : root(root)
        , previous(current) {
        if (root != nullptr) {
            root->resume_state.fetch_add(1, std::memory_order_acq_rel);
            current = this;
        }
    }

    ~resume_scope() {
        if (root == nullptr) {
            return;
        }
        current = previous;
        const std::uint32_t prior =
            root->resume_state.fetch_sub(1, std::memory_order_acq_rel);
        if ((prior & frame_chain_link::resume_count_mask) == 1U &&
            (prior & frame_chain_link::deferred_destroy_bit) != 0U) {
            const std::coroutine_handle<> frames = root->self_frame;
            destroy_frame_chain(frames, root);
        }
    }

    [[nodiscard]] static auto contains(frame_chain_link* root) noexcept -> bool {
        for (auto* scope = current; scope != nullptr; scope = scope->previous) {
            if (scope->root == root) {
                return true;
            }
        }
        return false;
    }

    frame_chain_link* root = nullptr;
    resume_scope* previous = nullptr;
    inline static thread_local resume_scope* current = nullptr;
};

inline auto resume_with_credit(
    std::coroutine_handle<> continuation,
    frame_chain_link* root) noexcept -> void {
    if (!continuation || continuation == std::noop_coroutine()) {
        return;
    }
    resume_scope scope{root};
    continuation.resume();
}

// Forge's backend-direct awaitables accept this extended continuation shape.
// It keeps the public io_awaitable coroutine_handle form intact while letting
// library backends bracket a cross-thread resume without another allocation.
struct resume_target {
    std::coroutine_handle<> continuation{};
    frame_chain_link* root = nullptr;

    auto resume() const noexcept -> void {
        resume_with_credit(continuation, root);
    }
};

template<class Awaitable>
class env_await_adapter {
public:
    explicit env_await_adapter(Awaitable awaitable, const io_env** env) noexcept(
        std::is_nothrow_constructible_v<Awaitable, Awaitable&&>)
        : awaitable_(static_cast<Awaitable&&>(awaitable))
        , env_(env)
    {}

    [[nodiscard]] auto await_ready() noexcept(noexcept(awaitable_.await_ready()))
        -> bool {
        return awaitable_.await_ready();
    }

    template<class Promise>
    auto await_suspend(std::coroutine_handle<Promise> continuation) {
        if constexpr (
            std::derived_from<Promise, frame_chain_link> &&
            requires(Awaitable& awaitable, resume_target target, const io_env* env) {
                awaitable.await_suspend(target, env);
            }) {
            auto* root = static_cast<frame_chain_link&>(
                continuation.promise()).root_link;
            return awaitable_.await_suspend(
                resume_target{continuation, root}, *env_);
        } else {
            return awaitable_.await_suspend(continuation, *env_);
        }
    }

    auto await_resume() noexcept(noexcept(awaitable_.await_resume()))
        -> decltype(auto) {
        return awaitable_.await_resume();
    }

private:
    Awaitable awaitable_;
    const io_env** env_;
};

// Arbitrates an abandoning destruction against the chain's only
// cross-thread resume source before any frame dies. While the chain is
// parked on a sender bridge, that bridge's completion slot is published on
// the root link. Claiming it as abandoned first means the delivery will
// only write result members and never resume. Losing the claim means a
// resume chain runs through the frames; destruction then waits for the
// chain's next stable state (finalized, parked on the next bridge, or
// parked on a non-bridge awaitable) and re-arbitrates. Chains parked on
// non-bridge awaitables keep their documented backend semantics (for
// example the io_uring and erased-stream terminate guards).
//
// An abandonment arriving on a resume call's own stack (a receiver
// completing inline destroyed the operation state, which is supported)
// must neither wait for that resume to unwind (self-deadlock) nor destroy
// frames it is still unwinding through: it defers the destruction to the
// tail of the last unwinding resume (sender_awaitable::complete).
template<class Promise>
inline auto abandon_task_chain(std::coroutine_handle<Promise> root) noexcept
    -> void {
    auto& link = static_cast<frame_chain_link&>(root.promise());
    if (link.started.load(std::memory_order_acquire)) {
        if (resume_scope::contains(&link)) {
            link.resume_state.fetch_or(
                frame_chain_link::deferred_destroy_bit,
                std::memory_order_acq_rel);
            return;
        }
        int spins = 0;
        for (;;) {
            if (link.finalized.load(std::memory_order_acquire)) {
                break;
            }
            const auto gate =
                link.active_bridge.load(std::memory_order_acquire);
            if (gate != 0) {
                auto expected = gate;
                if (!link.active_bridge.compare_exchange_weak(
                        expected,
                        frame_chain_link::with_bridge_state(
                            gate, bridge_state::abandoned),
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    continue;
                }
                if (frame_chain_link::bridge_state_of(gate) !=
                    bridge_state::completed) {
                    // Claimed first: the delivery can never resume the
                    // chain (the record-level teardown wait happens inside
                    // the frame destructors).
                    break;
                }
                // Lost to a delivery whose resume is (or will be) running
                // through the frames: wait until this bridge's frame moves
                // on, then re-evaluate the new state.
                while (frame_chain_link::same_bridge(
                           link.active_bridge.load(std::memory_order_acquire),
                           gate) &&
                       !link.finalized.load(std::memory_order_acquire)) {
                    abandon_backoff_step(spins);
                }
                continue;
            }
            if ((link.resume_state.load(std::memory_order_acquire) &
                 frame_chain_link::resume_count_mask) == 0U) {
                // Quiescent without a bridge: never suspended on one, or
                // parked on a non-bridge awaitable whose own abandonment
                // semantics apply.
                break;
            }
            // A resume is running between bridges; wait for progression.
            abandon_backoff_step(spins);
        }
        // Whatever ended the arbitration, a resume call may still be
        // unwinding out of the frames (the counter drops only after the
        // resume() call returns); the frames must outlive that unwind.
        while ((link.resume_state.load(std::memory_order_acquire) &
                frame_chain_link::resume_count_mask) != 0U) {
            abandon_backoff_step(spins);
        }
    }
    destroy_frame_chain(root, &root.promise());
}

template<class Promise>
struct promise_base : frame_chain_link {
    const io_env* env = nullptr;
    std::coroutine_handle<> continuation{};
    void* completion_object = nullptr;
    void (*complete)(void*) noexcept = nullptr;

    // P4127 explicit-parameter frame allocation: a coroutine whose parameter
    // list starts with (std::allocator_arg_t, std::pmr::memory_resource*)
    // allocates its frame from that resource; every other coroutine keeps
    // the global operator new path. Allocation failure propagates as an
    // exception; get_return_object_on_allocation_failure is intentionally
    // not provided so a throwing resource cannot be mistaken for an empty
    // task.
    //
    // Alignment boundary: coroutine frame allocation never selects
    // align_val_t overloads, so a frame whose contents need more than
    // __STDCPP_DEFAULT_NEW_ALIGNMENT__ (e.g. an over-aligned local or
    // parameter) receives default-aligned storage with no diagnostic;
    // measured on GCC 16 and Clang 19, an alignas(64) local can land at
    // offset 16 mod 64. Keep over-aligned state behind an indirection
    // (heap allocation or an aligned buffer) instead of frame locals.
    static auto operator new(std::size_t size) -> void* {
        void* frame = ::operator new(frame_allocation_size(size));
        stash_frame_resource(frame, size, nullptr);
        return frame;
    }

    template<class... Args>
    static auto operator new(
        std::size_t size,
        std::allocator_arg_t,
        std::pmr::memory_resource* memory,
        const Args&...) -> void* {
        if (memory == nullptr) {
            return operator new(size);
        }
        void* frame = memory->allocate(
            frame_allocation_size(size),
            frame_allocation_alignment);
        stash_frame_resource(frame, size, memory);
        return frame;
    }

    // [dcl.fct.def.coroutine]/9 passes the implicit object parameter of a
    // member or lambda coroutine ahead of the declared parameters (GCC
    // does; current Clang does not), so without this overload the same
    // lambda coroutine silently fell back to the global heap on one
    // compiler and used the resource on the other. Mirroring
    // std::generator's allocator protocol, any first argument in front of
    // the (allocator_arg, resource) pair is treated as the object
    // parameter.
    template<class This, class... Args>
    static auto operator new(
        std::size_t size,
        const This&,
        std::allocator_arg_t,
        std::pmr::memory_resource* memory,
        const Args&...) -> void* {
        return operator new(size, std::allocator_arg, memory);
    }

    static auto operator delete(void* frame, std::size_t size) noexcept
        -> void {
        std::pmr::memory_resource* memory = stashed_frame_resource(frame, size);
        if (memory == nullptr) {
            ::operator delete(frame, frame_allocation_size(size));
            return;
        }
        memory->deallocate(
            frame,
            frame_allocation_size(size),
            frame_allocation_alignment);
    }

    [[nodiscard]] auto initial_suspend() noexcept -> std::suspend_always {
        return {};
    }

    struct final_awaiter {
        [[nodiscard]] auto await_ready() noexcept -> bool {
            return false;
        }

        template<class P>
        auto await_suspend(std::coroutine_handle<P> continuation) noexcept
            -> std::coroutine_handle<> {
            auto& promise = continuation.promise();
            // Copy everything needed out of the frame before publishing
            // finalized: the completion callback below may synchronously
            // destroy the whole operation state including this frame (a
            // receiver completing inline is allowed to drop the op), so no
            // promise member may be touched after the callback runs. An
            // abandoning thread that observes finalized still waits for
            // the resuming counter to drain before destroying frames, so
            // publishing it here does not expose the frame early.
            const auto complete_fn = promise.complete;
            void* const completion_object = promise.completion_object;
            const std::coroutine_handle<> next = promise.continuation
                ? promise.continuation
                : std::noop_coroutine();
            promise.finalized.store(true, std::memory_order_release);
            if (complete_fn != nullptr) {
                complete_fn(completion_object);
                return std::noop_coroutine();
            }
            return next;
        }

        auto await_resume() noexcept -> void {}
    };

    [[nodiscard]] auto final_suspend() noexcept -> final_awaiter {
        return {};
    }

    template<class T>
    [[nodiscard]] auto await_transform(io_task<T>&& task) {
        return task_awaitable<T>{std::move(task), &env, this};
    }

    template<class Awaitable>
        requires io_awaitable<Awaitable>
    [[nodiscard]] auto await_transform(Awaitable&& awaitable) {
        using awaitable_t = std::conditional_t<
            std::is_lvalue_reference_v<Awaitable>,
            Awaitable,
            std::remove_cvref_t<Awaitable>>;
        return env_await_adapter<awaitable_t>{
            static_cast<Awaitable&&>(awaitable),
            &env};
    }

    template<class Awaitable>
        requires (!io_awaitable<Awaitable>)
    [[nodiscard]] decltype(auto) await_transform(Awaitable&& awaitable) {
        if constexpr (std::is_lvalue_reference_v<Awaitable>) {
            return static_cast<Awaitable&&>(awaitable);
        } else {
            using awaitable_t = std::remove_cvref_t<Awaitable>;
            return awaitable_t{static_cast<Awaitable&&>(awaitable)};
        }
    }
};

template<class T>
using task_result_storage = std::variant<std::monostate, T, std::exception_ptr>;

} // namespace __coro_detail

} // namespace forge::io

#endif // __cpp_impl_coroutine
