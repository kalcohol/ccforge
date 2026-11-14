#include <simd>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error This benchmark must measure the backport in a mode without native SIMD declarations.
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <type_traits>

// The benchmark uses compiler barriers to keep every result observable without
// putting volatile lane accesses or checksum reductions in the timed kernel.
#if defined(__GNUC__) || defined(__clang__)
template<class T> inline void observe(const T& value) {
    asm volatile("" : : "m"(value) : "memory");
}
template<class T> inline void input_barrier(T& value) {
    asm volatile("" : "+m"(value) : : "memory");
}
// Timed lambdas are always inlined so that inlining heuristics, which react to
// the different mask sizes, cannot decide the comparison between builds.
#define FORGE_BENCH_INLINE __attribute__((always_inline))
#else
#error This measurement harness requires GCC or Clang compiler barriers.
#endif

struct minimum {
    template<class V> V operator()(const V& a, const V& b) const {
        return std::simd::select(a < b, a, b);
    }
};
struct maximum {
    template<class V> V operator()(const V& a, const V& b) const {
        return std::simd::select(a > b, a, b);
    }
};

template<class F>
void measure(std::string_view type, std::string_view abi, int lanes,
             std::string_view operation, std::size_t iterations, F kernel) {
    for (int i = 0; i < 1024; ++i) kernel();
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) kernel();
    const auto ns = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "simd_bench,type=" << type << ",abi=" << abi
              << ",lanes=" << lanes << ",op=" << operation
              << ",iterations=" << iterations << ",ns=" << std::setprecision(17) << ns << '\n';
}

template<class V>
void run(std::string_view type, std::string_view abi, std::size_t iterations) {
    using T = typename V::value_type;
    // Bounded inputs avoid integer overflow and transcendental range errors.
    // Cache-line aligned operands keep stack placement from favouring either
    // build; the mask representations have different sizes.
    alignas(64) V a([](auto i) { return T(1) + T(int(i) % 3); });
    alignas(64) V b(T(2)), c(T(1));
    alignas(64) V zero(T(0)), three(T(3));
    // Only one lane is 2: even the widest integer product is representable.
    alignas(64) V product([](auto i) { return T(int(i) == 0 ? 2 : 1); });
    alignas(64) typename V::mask_type mask = a > V(T(1));
    alignas(64) typename V::mask_type mask_full = a > zero, mask_empty = a > three;
    alignas(64) typename V::mask_type mask_last([](auto i) { return int(i) == V::size() - 1; });
    alignas(64) typename V::mask_type mask_first([](auto i) { return int(i) == 0; });
    alignas(64) std::array<T, V::size()> input{}, output{};
    for (std::size_t i = 0; i < input.size(); ++i) input[i] = T(1 + i % 3);
    auto bench = [&](std::string_view name, auto f) {
        measure(type, abi, V::size(), name, iterations, [&]() FORGE_BENCH_INLINE {
            // Read/write operands also hide non-escaping local constants.
            input_barrier(a);
            input_barrier(b);
            input_barrier(c);
            input_barrier(product);
            input_barrier(mask);
            input_barrier(input);
            input_barrier(zero);
            input_barrier(three);
            input_barrier(mask_full);
            input_barrier(mask_empty);
            input_barrier(mask_last);
            input_barrier(mask_first);
            observe(f());
        });
    };
    bench("barrier", [&] { return a; });
    bench("add", [&] { return a + b; });
    bench("subtract", [&] { return a - b; });
    bench("multiply", [&] { return a * b; });
    bench("divide", [&] { return a / b; });
    bench("reduce_plus", [&] { return std::simd::reduce(a); });
    bench("reduce_multiplies", [&] { return std::simd::reduce(product, std::multiplies<>{}); });
    bench("reduce_min_op", [&] { return std::simd::reduce(a, minimum{}); });
    bench("reduce_max_op", [&] { return std::simd::reduce(a, maximum{}); });
    bench("reduce_min", [&] { return std::simd::reduce_min(a); });
    bench("reduce_max", [&] { return std::simd::reduce_max(a); });
    bench("masked_reduce_plus", [&] { return std::simd::reduce(a, mask); });
    bench("all_of", [&] { return std::simd::all_of(mask); });
    bench("any_of", [&] { return std::simd::any_of(mask); });
    bench("none_of", [&] { return std::simd::none_of(mask); });
    bench("compare_all_of", [&] { return std::simd::all_of(a > c); });
    bench("compare_any_of", [&] { return std::simd::any_of(a > c); });
    bench("compare_none_of", [&] { return std::simd::none_of(a > c); });
    // Original horizontal rows decide at lane 0 or 1, favouring early exits.
    // The full_* rows measure the opposite case: every lane must be scanned.
    bench("full_all_of", [&] { return std::simd::all_of(mask_full); });
    bench("full_any_of", [&] { return std::simd::any_of(mask_empty); });
    bench("full_none_of", [&] { return std::simd::none_of(mask_empty); });
    bench("full_compare_all_of", [&] { return std::simd::all_of(a > zero); });
    bench("full_compare_any_of", [&] { return std::simd::any_of(a > three); });
    bench("full_compare_none_of", [&] { return std::simd::none_of(a > three); });
    // The original mask is empty for one lane; use the nonempty full mask there.
    bench("reduce_min_index", [&] {
        if constexpr (V::size() == 1) return std::simd::reduce_min_index(mask_full);
        else return std::simd::reduce_min_index(mask);
    });
    bench("reduce_max_index", [&] {
        if constexpr (V::size() == 1) return std::simd::reduce_max_index(mask_full);
        else return std::simd::reduce_max_index(mask);
    });
    bench("full_reduce_min_index", [&] { return std::simd::reduce_min_index(mask_last); });
    bench("full_reduce_max_index", [&] { return std::simd::reduce_max_index(mask_first); });
    bench("reduce_count", [&] { return std::simd::reduce_count(mask); });
    bench("select", [&] { return std::simd::select(mask, a, b); });
    bench("compare_select_min", [&] { return std::simd::select(a < b, a, b); });
    bench("compare_select_max", [&] { return std::simd::select(a > b, a, b); });
    bench("compare_select_clamp", [&] {
        return std::simd::select(a < c, c, std::simd::select(a > b, b, a));
    });
    bench("mask_and_select", [&] { return std::simd::select((a > c) && (a < b), a, b); });
    bench("mask_or_select", [&] { return std::simd::select((a < b) || (a > c), a, b); });
    bench("compare_masked_reduce", [&] { return std::simd::reduce(a, a > c); });
    bench("compare_reduce_count", [&] { return std::simd::reduce_count(a > c); });
    if constexpr (std::is_same_v<T, float>) {
        using D = std::simd::vec<double, V::size()>;
        alignas(64) D da([](auto i) { return double(1 + int(i) % 3); }), db(2.0);
        measure(type, abi, V::size(), "convert_mask_select_double", iterations, [&]() FORGE_BENCH_INLINE {
            input_barrier(a);
            input_barrier(b);
            input_barrier(da);
            input_barrier(db);
            observe(std::simd::select(typename D::mask_type(a < b), da, db));
        });
    }
    bench("load", [&] { return std::simd::unchecked_load<V>(input.data()); });
    measure(type, abi, V::size(), "store", iterations, [&]() FORGE_BENCH_INLINE {
        input_barrier(a);
        std::simd::unchecked_store(a, output.data());
        observe(output);
    });
    measure(type, abi, V::size(), "masked_store", iterations, [&]() FORGE_BENCH_INLINE {
        input_barrier(a);
        input_barrier(mask);
        std::simd::unchecked_store(a, output.data(), mask);
        observe(output);
    });
    if constexpr (std::is_floating_point_v<T>) {
        bench("sqrt", [&] { return std::simd::sqrt(a); });
        bench("sin", [&] { return std::simd::sin(a); });
        bench("exp", [&] { return std::simd::exp(a); });
        bench("fma", [&] { return std::simd::fma(a, b, c); });
    }
}

template<class T>
void sizes(std::string_view type, std::size_t iterations) {
    run<std::simd::basic_vec<T>>(type, "native", iterations);
    run<std::simd::basic_vec<T, std::simd::__fixed_size_abi<1>>>(type, "fixed", iterations);
    run<std::simd::basic_vec<T, std::simd::__fixed_size_abi<3>>>(type, "fixed", iterations);
    run<std::simd::basic_vec<T, std::simd::__fixed_size_abi<4>>>(type, "fixed", iterations);
    run<std::simd::basic_vec<T, std::simd::__fixed_size_abi<8>>>(type, "fixed", iterations);
    run<std::simd::basic_vec<T, std::simd::__fixed_size_abi<16>>>(type, "fixed", iterations);
}

int main(int argc, char** argv) {
    const std::size_t iterations = argc == 2 ? std::strtoull(argv[1], nullptr, 10) : 1000000;
    if (argc > 2 || iterations == 0) return 2;
    sizes<float>("float", iterations);
    sizes<double>("double", iterations);
    sizes<std::int32_t>("int32", iterations);
}
