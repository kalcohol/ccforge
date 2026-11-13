#include <linalg>
#include <mdspan>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
using ext = std::dextents<std::size_t, 2>;
#if defined(__GNUC__) || defined(__clang__)
#define FORGE_BENCH_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define FORGE_BENCH_NOINLINE __declspec(noinline)
#else
#define FORGE_BENCH_NOINLINE
#endif

template<class A, class B, class C>
FORGE_BENCH_NOINLINE void product(A a, B b, C c, std::string_view op) {
    if (op == "gemm") std::linalg::matrix_product(a, b, c);
    else if (op == "trmm_left")
        std::linalg::triangular_matrix_left_product(a, std::linalg::lower_triangle,
            std::linalg::explicit_diagonal, c);
    else
        std::linalg::triangular_matrix_right_product(a, std::linalg::lower_triangle,
            std::linalg::explicit_diagonal, c);
}

template<class T, class AL, class BL>
void measure(std::size_t n, int samples, std::string_view op) {
    std::vector<T> av(n * n), bv(n * n), cv(n * n);
    std::mdspan<T, ext, AL> a(av.data(), n, n);
    std::mdspan<T, ext, BL> b(bv.data(), n, n);
    // For TRMM, B's layout denotes the layout of the in-place matrix C.
    std::mdspan<T, ext, BL> c(cv.data(), n, n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            a[i, j] = T((int((i * 7 + j * 3) % 17) - 8) / 32.0);
            b[i, j] = T((int((i * 3 + j * 5) % 19) - 9) / 32.0);
        }
    for (int sample = -1; sample < samples; ++sample) {
        std::copy(bv.begin(), bv.end(), cv.begin()); // Reset outside timing, including for in-place products.
        const auto start = std::chrono::steady_clock::now();
        product(a, b, c, op);
        const auto finish = std::chrono::steady_clock::now();
        double checksum = 0;
        for (auto v : cv) checksum += double(v);
        if (sample >= 0) {
            const double ns = std::chrono::duration<double, std::nano>(finish - start).count();
            std::cout << "{\"op\":\"" << op << "\",\"type\":\""
                      << (std::is_same_v<T, double> ? "double" : "float")
                      << "\",\"a_layout\":\"" << (std::is_same_v<AL, std::layout_right> ? "right" : "left")
                      << "\",\"b_layout\":\"" << (std::is_same_v<BL, std::layout_right> ? "right" : "left")
                      << "\",\"n\":" << n << ",\"sample\":" << sample
                      << ",\"ns\":" << ns << ",\"checksum\":" << checksum << "}\n";
        }
    }
}

template<class T> void suite(std::size_t n, int samples) {
    for (auto op : {"gemm", "trmm_left", "trmm_right"}) {
        measure<T, std::layout_right, std::layout_right>(n, samples, op);
        measure<T, std::layout_right, std::layout_left>(n, samples, op);
        measure<T, std::layout_left, std::layout_right>(n, samples, op);
        measure<T, std::layout_left, std::layout_left>(n, samples, op);
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 3 || (argc > 1 && std::string_view(argv[1]) == "--help")) {
        std::cerr << "usage: measure_products [size: 1..4096, 0=64/256/512] [samples: 1..1000]\n";
        return argc > 3 ? 2 : 0;
    }
    const long size = argc > 1 ? std::strtol(argv[1], nullptr, 10) : 0;
    const long samples = argc > 2 ? std::strtol(argv[2], nullptr, 10) : 5;
    if (size < 0 || size > 4096 || samples < 1 || samples > 1000) return 2;
    std::cout << std::setprecision(17);
    for (std::size_t n : {64u, 256u, 512u}) {
        if (size) n = static_cast<std::size_t>(size);
        suite<double>(n, static_cast<int>(samples));
        suite<float>(n, static_cast<int>(samples));
        if (size) break;
    }
}
