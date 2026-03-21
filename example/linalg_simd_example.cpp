#include <linalg>
#include <mdspan>
#include <cassert>
#include <cmath>

int main() {
    constexpr int N = 256;
    double x[N], y[N];
    for (int i = 0; i < N; ++i) { x[i] = static_cast<double>(i+1); y[i] = 1.0/(i+1); }

    std::mdspan xv(x, std::extents<int, N>{});
    std::mdspan yv(y, std::extents<int, N>{});

    // dot product (SIMD-accelerated when simd backport available)
    double d = std::linalg::dot(xv, yv, 0.0);
    assert(std::abs(d - N) < 1e-10);

    // vector_two_norm (SIMD-accelerated)
    float fdata[4] = {3.0f, 4.0f, 0.0f, 0.0f};
    std::mdspan fv(fdata, std::extents<int, 4>{});
    float nrm = std::linalg::vector_two_norm(fv);
    assert(std::abs(nrm - 5.0f) < 1e-5f);

    return 0;
}
