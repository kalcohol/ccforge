// Test-only global allocation replacements: include in exactly one TU per executable.
#pragma once

#if defined(__SANITIZE_THREAD__)
#define FORGE_TEST_STATIC_OPSTATE_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define FORGE_TEST_STATIC_OPSTATE_TSAN 1
#endif
#endif

#if defined(FORGE_TEST_STATIC_OPSTATE_TSAN)
#include <cstdio>

int main() {
    std::fprintf(stderr,
        "Skipped: ThreadSanitizer defines the global allocation replacements used by this test.\n");
    return 77;
}
#else

#include <forge/execution_extensions.hpp>
#include <gtest/gtest.h>
#include <execution>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

namespace allocation_probe {
std::atomic<bool> enabled{false};
std::atomic<std::size_t> calls{0};
inline thread_local bool fail_allocations = false;

void count() noexcept {
    if (enabled.load(std::memory_order_relaxed)) {
        calls.fetch_add(1, std::memory_order_relaxed);
    }
}

void* allocate(std::size_t size) {
    count();
    if (fail_allocations) throw std::bad_alloc{};
    for (;;) {
        if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
        if (auto handler = std::get_new_handler()) handler();
        else throw std::bad_alloc{};
    }
}

// Use malloc/free for both paths on every platform, including Windows. The
// original allocation is retained immediately before the aligned address.
void* allocate_aligned(std::size_t size, std::size_t alignment) {
    if (alignment < alignof(void*)) alignment = alignof(void*);
    const auto extra = alignment - 1 + sizeof(void*);
    if (size > (std::numeric_limits<std::size_t>::max)() - extra) {
        count();
        throw std::bad_alloc{};
    }
    void* raw = allocate(size + extra);
    auto address = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
    address = (address + alignment - 1) & ~(alignment - 1);
    void* aligned = reinterpret_cast<void*>(address);
    static_cast<void**>(aligned)[-1] = raw;
    return aligned;
}

void free_aligned(void* p) noexcept {
    if (p) std::free(static_cast<void**>(p)[-1]);
}

struct window {
    window() noexcept {
        calls.store(0, std::memory_order_relaxed);
        enabled.store(true, std::memory_order_relaxed);
    }
    ~window() { enabled.store(false, std::memory_order_relaxed); }
};

template<class F>
std::size_t measure(F&& f) {
    {
        window scope;
        std::forward<F>(f)();
    }
    return calls.load(std::memory_order_relaxed);
}
} // namespace allocation_probe

// Keep these replacements confined to this executable. No googletest calls
// occur while a measurement window is open.
void* operator new(std::size_t n) { return allocation_probe::allocate(n); }
void* operator new[](std::size_t n) { return allocation_probe::allocate(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate(n); } catch (...) { return nullptr; }
}
void* operator new(std::size_t n, std::align_val_t a) {
    return allocation_probe::allocate_aligned(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return allocation_probe::allocate_aligned(n, static_cast<std::size_t>(a));
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate_aligned(n, static_cast<std::size_t>(a)); }
    catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate_aligned(n, static_cast<std::size_t>(a)); }
    catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { allocation_probe::free_aligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { allocation_probe::free_aligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    allocation_probe::free_aligned(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    allocation_probe::free_aligned(p);
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::free_aligned(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::free_aligned(p);
}

namespace allocation_probe {
struct calibration_callback {
    // Match the pointer-sized when_all callback without doing any work.
    void* state = nullptr;
    void operator()() const noexcept {}
};

std::size_t allocations_per_registration() {
    std::inplace_stop_source source;
    const auto allocations = allocation_probe::measure([&] {
        std::inplace_stop_callback<calibration_callback> callback(
            source.get_token(), calibration_callback{});
    });
    EXPECT_EQ(allocations, 0u);
    return allocations;
}

} // namespace allocation_probe

#endif // defined(FORGE_TEST_STATIC_OPSTATE_TSAN)
