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
    V a([](auto i) { return T(1) + T(int(i) % 3); });
    V b(T(2)), c(T(1));
    // Only one lane is 2: even the widest integer product is representable.
    V product([](auto i) { return T(int(i) == 0 ? 2 : 1); });
    typename V::mask_type mask = a > V(T(1));
    std::array<T, V::size()> input{}, output{};
    for (std::size_t i = 0; i < input.size(); ++i) input[i] = T(1 + i % 3);
    auto bench = [&](std::string_view name, auto f) {
        measure(type, abi, V::size(), name, iterations, [&] {
            // Read/write operands also hide non-escaping local constants.
            input_barrier(a);
            input_barrier(b);
            input_barrier(c);
            input_barrier(product);
            input_barrier(mask);
            input_barrier(input);
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
    bench("reduce_count", [&] { return std::simd::reduce_count(mask); });
    bench("select", [&] { return std::simd::select(mask, a, b); });
    bench("load", [&] { return std::simd::unchecked_load<V>(input.data()); });
    measure(type, abi, V::size(), "store", iterations, [&] {
        input_barrier(a);
        std::simd::unchecked_store(a, output.data());
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
    run<std::simd::basic_vec<T, std::simd::fixed_size_abi<1>>>(type, "fixed", iterations);
    run<std::simd::basic_vec<T, std::simd::fixed_size_abi<3>>>(type, "fixed", iterations);
    run<std::simd::basic_vec<T, std::simd::fixed_size_abi<4>>>(type, "fixed", iterations);
    run<std::simd::basic_vec<T, std::simd::fixed_size_abi<8>>>(type, "fixed", iterations);
    run<std::simd::basic_vec<T, std::simd::fixed_size_abi<16>>>(type, "fixed", iterations);
}

int main(int argc, char** argv) {
    const std::size_t iterations = argc == 2 ? std::strtoull(argv[1], nullptr, 10) : 1000000;
    if (argc > 2 || iterations == 0) return 2;
    sizes<float>("float", iterations);
    sizes<double>("double", iterations);
    sizes<std::int32_t>("int32", iterations);
}
