#include <simd>

#include <complex>

int main() {
    using float4 = std::simd::vec<float, 4>;
    using complex4 = std::simd::vec<std::complex<float>, 4>;

    complex4 values(float4(1.0f), float4(2.0f));
    auto reals = std::simd::real(values);
    auto imags = std::simd::imag(values);
    auto conjugated = std::simd::conj(values);
    auto magnitudes = std::simd::abs(values);
    auto logged = std::simd::log10(values);
    auto powered = std::simd::pow(values, values);
    auto polar_value = std::simd::polar(reals, imags);

    (void)conjugated;
    (void)logged;
    (void)powered;
    (void)polar_value;
    return static_cast<int>(reals[0] + imags[0] + magnitudes[0]);
}
