// SIMD14_RANGE: 0 C array, 1 std::array, 2 static span, 3 noncopyable fixed view,
//              4 derived std::array, 5 cv-dependent view (const has the selected extent).
// SIMD14_OPERATION: 0 explicit load, 1 inferred load, 2 store.
// SIMD14_MASKED: 0 unmasked, 1 all-false mask. SIMD14_RVALUE: 0 lvalue, 1 rvalue.
// SIMD14_ACCEPT: 0 short (must reject), 1 exact width (must accept).
#include <simd>

#include <array>
#include <cstddef>
#include <span>
#include <utility>

#ifndef SIMD14_RANGE
#define SIMD14_RANGE 0
#endif
#ifndef SIMD14_OPERATION
#define SIMD14_OPERATION 0
#endif
#ifndef SIMD14_MASKED
#define SIMD14_MASKED 0
#endif
#ifndef SIMD14_RVALUE
#define SIMD14_RVALUE 0
#endif
#ifndef SIMD14_ACCEPT
#define SIMD14_ACCEPT 0
#endif

using vector_type = std::simd::vec<int, 4>;
using native_type = std::simd::basic_vec<int>;
constexpr std::size_t width = SIMD14_OPERATION == 1 ? native_type::size : vector_type::size;
constexpr std::size_t count = SIMD14_ACCEPT ? width : width - 1;

struct fixed_view {
    int* first;

    explicit constexpr fixed_view(int* p) : first(p) {}
    fixed_view() = delete;
    fixed_view(const fixed_view&) = delete;
    fixed_view(fixed_view&&) = delete;
    constexpr int* begin() const noexcept { return first; }
    constexpr int* end() const noexcept { return first + count; }
    constexpr std::size_t size() const noexcept { return count; }
};

struct derived_array : std::array<int, count> {};

struct cv_view {
    int* first;

    constexpr int* begin() const noexcept { return first; }
    constexpr int* end() noexcept { return first + width; }
    constexpr int* end() const noexcept { return first + count; }
    constexpr std::size_t size() noexcept { return width; }
    constexpr std::size_t size() const noexcept { return count; }
};

template<class R>
void exercise(R&& range) {
#if SIMD14_OPERATION == 0
#if SIMD14_MASKED
    // Explicit R keeps the separate masked C-array overload ambiguity out of this probe.
    (void)std::simd::unchecked_load<vector_type, R>(std::forward<R>(range), vector_type::mask_type(false));
#else
    (void)std::simd::unchecked_load<vector_type>(std::forward<R>(range));
#endif
#elif SIMD14_OPERATION == 1
#if SIMD14_MASKED
    (void)std::simd::unchecked_load<R>(std::forward<R>(range), native_type::mask_type(false));
#else
    (void)std::simd::unchecked_load(std::forward<R>(range));
#endif
#else
#if SIMD14_MASKED
    std::simd::unchecked_store(vector_type(7), std::forward<R>(range), vector_type::mask_type(false));
#else
    std::simd::unchecked_store(vector_type(7), std::forward<R>(range));
#endif
#endif
}

int main() {
#if SIMD14_RANGE == 0
    int range[count]{};
#elif SIMD14_RANGE == 1
    std::array<int, count> range{};
#elif SIMD14_RANGE == 2
    std::array<int, count> storage{};
    std::span<int, count> range(storage);
#elif SIMD14_RANGE == 3
    std::array<int, count> storage{};
    fixed_view range(storage.data());
#elif SIMD14_RANGE == 4
    derived_array range{};
#else
    std::array<int, width> storage{};
    const cv_view range{storage.data()};
#endif
#if SIMD14_RVALUE
    exercise(std::move(range));
#else
    exercise(range);
#endif
}
