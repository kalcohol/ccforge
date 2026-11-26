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
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

// Reserved for a future L0 model-checking backend. Failing explicitly keeps a
// model build from silently selecting the real primitives.
#if defined(FORGE_EXECUTION_SYNC_SHIM_MODEL) && FORGE_EXECUTION_SYNC_SHIM_MODEL
#error "The execution sync shim model backend is not implemented"
#endif

// The portable-mode override also lets the compile probe exercise the no-op
// definitions on a Clang toolchain.
#if defined(__clang__) && !defined(FORGE_EXECUTION_SYNC_SHIM_NO_ANNOTATIONS)
#define FORGE_EXECUTION_SYNC_ANNOTATIONS_ACTIVE 1
#define FORGE_EXECUTION_SYNC_CAPABILITY(name) __attribute__((capability(name)))
#define FORGE_EXECUTION_SYNC_SCOPED_CAPABILITY __attribute__((scoped_lockable))
#define FORGE_EXECUTION_SYNC_ACQUIRE(...) __attribute__((acquire_capability(__VA_ARGS__)))
#define FORGE_EXECUTION_SYNC_TRY_ACQUIRE(...) __attribute__((try_acquire_capability(__VA_ARGS__)))
#define FORGE_EXECUTION_SYNC_RELEASE(...) __attribute__((release_capability(__VA_ARGS__)))
#define FORGE_EXECUTION_SYNC_GUARDED_BY(...) __attribute__((guarded_by(__VA_ARGS__)))
#define FORGE_EXECUTION_SYNC_REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))
#else
#define FORGE_EXECUTION_SYNC_ANNOTATIONS_ACTIVE 0
#define FORGE_EXECUTION_SYNC_CAPABILITY(name)
#define FORGE_EXECUTION_SYNC_SCOPED_CAPABILITY
#define FORGE_EXECUTION_SYNC_ACQUIRE(...)
#define FORGE_EXECUTION_SYNC_TRY_ACQUIRE(...)
#define FORGE_EXECUTION_SYNC_RELEASE(...)
#define FORGE_EXECUTION_SYNC_GUARDED_BY(...)
#define FORGE_EXECUTION_SYNC_REQUIRES(...)
#endif

// The shim's own lock and unlock bodies call the native mutex, which some
// standard libraries annotate as a capability of its own. Keep those bodies
// out of Clang's analysis in both annotation modes. The mode above controls
// only the shim's own capability annotations.
#if defined(__clang__)
#define FORGE_EXECUTION_SYNC_NO_ANALYSIS __attribute__((no_thread_safety_analysis))
#else
#define FORGE_EXECUTION_SYNC_NO_ANALYSIS
#endif

namespace std::execution::__forge_sync {

// Declare the befriended shim classes first: in a namespace nested inside
// std, MSVC binds an unqualified friend class name to std::unique_lock or
// std::condition_variable instead of declaring a namespace member.
class unique_lock;
class condition_variable;

template<class T>
using atomic = std::atomic<T>; // Sequentially consistent unless explicitly overridden.
using thread = std::thread;
using steady_clock = std::chrono::steady_clock;

class FORGE_EXECUTION_SYNC_CAPABILITY("mutex") mutex {
public:
    // The capability is this shim mutex, even if the standard library
    // annotates the underlying native mutex as a separate capability.
    void lock() FORGE_EXECUTION_SYNC_ACQUIRE() FORGE_EXECUTION_SYNC_NO_ANALYSIS { native_.lock(); }
    bool try_lock() FORGE_EXECUTION_SYNC_TRY_ACQUIRE(true) FORGE_EXECUTION_SYNC_NO_ANALYSIS {
        return native_.try_lock();
    }
    void unlock() FORGE_EXECUTION_SYNC_RELEASE() FORGE_EXECUTION_SYNC_NO_ANALYSIS {
        native_.unlock();
    }

private:
    friend class unique_lock;
    std::mutex native_;
};

// A deliberately small, always-acquired scoped lock. Waiting temporarily
// releases it inside the condition variable and reacquires it on return.
class FORGE_EXECUTION_SYNC_SCOPED_CAPABILITY unique_lock {
public:
    explicit unique_lock(mutex& gate) FORGE_EXECUTION_SYNC_ACQUIRE(gate) FORGE_EXECUTION_SYNC_NO_ANALYSIS
        : native_(gate.native_) {}

    unique_lock(const unique_lock&) = delete;
    unique_lock& operator=(const unique_lock&) = delete;
    ~unique_lock() FORGE_EXECUTION_SYNC_RELEASE() = default;

private:
    friend class condition_variable;
    std::unique_lock<std::mutex> native_;
};

class condition_variable {
public:
    void wait(unique_lock& lock) { native_.wait(lock.native_); }

    template<class Predicate>
    void wait(unique_lock& lock, Predicate&& ready) {
        native_.wait(lock.native_, std::forward<Predicate>(ready));
    }

    auto wait_until(unique_lock& lock, steady_clock::time_point deadline) -> std::cv_status {
        return native_.wait_until(lock.native_, deadline);
    }

    void notify_one() noexcept { native_.notify_one(); }
    void notify_all() noexcept { native_.notify_all(); }

private:
    std::condition_variable native_;
};

} // namespace std::execution::__forge_sync

#undef FORGE_EXECUTION_SYNC_NO_ANALYSIS
