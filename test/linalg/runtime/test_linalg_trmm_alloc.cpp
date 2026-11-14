// Global allocation replacements are confined to this test executable.
#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

#if defined(__SANITIZE_THREAD__)
#define FORGE_TEST_TRMM_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define FORGE_TEST_TRMM_TSAN 1
#endif
#endif

#if defined(FORGE_TEST_TRMM_TSAN)
TEST(LinalgTrmmAlloc, LayoutsAndViews) {
    GTEST_SKIP() << "ThreadSanitizer supplies global allocation replacements";
}
#else

namespace allocation_probe {
std::atomic<bool> enabled{false};
std::atomic<std::size_t> calls{0};

void count() noexcept {
    if (enabled.load(std::memory_order_relaxed)) {
        calls.fetch_add(1, std::memory_order_relaxed);
    }
}

void* allocate(std::size_t size) {
    count();
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

namespace {
using ext = std::dextents<std::size_t, 2>;

template<class L> auto make_mapping() {
    if constexpr (std::is_same_v<L, std::layout_stride>)
        return typename L::template mapping<ext>(ext(33, 33),
            std::array<std::size_t, 2>{69, 2});
    else return typename L::template mapping<ext>(ext(33, 33));
}

template<class A, class C, class Triangle, class Diagonal>
void measure_trmm(A a, C c, Triangle triangle, Diagonal diagonal) {
    auto fill_output = [&] {
        for (std::size_t i = 0; i < 33; ++i)
            for (std::size_t j = 0; j < 33; ++j) c[i, j] = double(i + j + 1) / 33;
    };
    fill_output();
    const auto left = allocation_probe::measure([&] {
        std::linalg::triangular_matrix_left_product(a, triangle, diagonal, c);
    });
    EXPECT_EQ(left, 0u);
    EXPECT_NE((c[0, 0]), 0.0);
    fill_output();
    const auto right = allocation_probe::measure([&] {
        std::linalg::triangular_matrix_right_product(a, triangle, diagonal, c);
    });
    EXPECT_EQ(right, 0u);
    EXPECT_NE((c[0, 0]), 0.0);
}

template<class AL, class CL, class Triangle>
void allocation_case(Triangle triangle) {
    using namespace std::linalg;
    std::array<double, 2400> input{}, output{};
    std::mdspan<double, ext, AL> a(input.data(), make_mapping<AL>());
    std::mdspan<double, ext, CL> c(output.data(), make_mapping<CL>());
    constexpr bool upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (std::size_t i = 0; i < 33; ++i)
        for (std::size_t j = 0; j < 33; ++j)
            if (upper ? i <= j : i >= j) a[i, j] = double(i + j + 1) / 33;
    measure_trmm(a, c, triangle, explicit_diagonal);
    measure_trmm(a, c, triangle, implicit_unit_diagonal);
    measure_trmm(conjugated(scaled(2.0, a)), c, triangle, explicit_diagonal);
    measure_trmm(conjugated(scaled(2.0, a)), c, triangle, implicit_unit_diagonal);
}

template<class AL, class CL> void dense_case() {
    allocation_case<AL, CL>(std::linalg::upper_triangle);
    allocation_case<AL, CL>(std::linalg::lower_triangle);
}

TEST(LinalgTrmmAlloc, ProbeCalibration) {
    void* memory = nullptr;
    const auto calls = allocation_probe::measure([&] { memory = ::operator new(128); });
    ::operator delete(memory);
    EXPECT_EQ(calls, 1u);
}

TEST(LinalgTrmmAlloc, LayoutsAndViews) {
    dense_case<std::layout_right, std::layout_right>();
    dense_case<std::layout_right, std::layout_left>();
    dense_case<std::layout_left, std::layout_right>();
    dense_case<std::layout_left, std::layout_left>();
    dense_case<std::layout_stride, std::layout_stride>();
    dense_case<std::layout_right_padded<8>, std::layout_right_padded<8>>();
    dense_case<std::layout_left_padded<8>, std::layout_left_padded<8>>();
    using namespace std::linalg;
    allocation_case<layout_blas_packed<upper_triangle_t, row_major_t>,
        std::layout_right>(upper_triangle);
    allocation_case<layout_blas_packed<lower_triangle_t, column_major_t>,
        std::layout_left>(lower_triangle);
}
} // namespace

#endif // defined(FORGE_TEST_TRMM_TSAN)
