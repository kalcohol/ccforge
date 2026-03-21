#include <simd>

#include <complex>
#include <span>
#include <type_traits>

int main() {
    using complex_default = std::simd::basic_vec<std::complex<float>>;

    static_assert(std::is_same_v<decltype(std::simd::partial_load(static_cast<const std::complex<float>*>(nullptr),
                                                                  std::simd::simd_size_type{})),
                                 complex_default>);
    static_assert(std::is_same_v<decltype(std::simd::partial_load(static_cast<const std::complex<float>*>(nullptr),
                                                                  static_cast<const std::complex<float>*>(nullptr))),
                                 complex_default>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const std::complex<float>*>(nullptr))),
                                 complex_default>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const std::complex<float>*>(nullptr),
                                                                    std::simd::simd_size_type{})),
                                 complex_default>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const std::complex<float>*>(nullptr),
                                                                    static_cast<const std::complex<float>*>(nullptr),
                                                                    std::simd::flag_default)),
                                 complex_default>);
    static_assert(std::is_same_v<decltype(std::simd::partial_load(std::declval<std::span<const std::complex<float>>>())),
                                 complex_default>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_load(std::declval<std::span<const std::complex<float>>>(),
                                                                    std::simd::flag_default)),
                                 complex_default>);

#if defined(__SIZEOF_INT128__)
    using int128 = __int128;
    using int128_default = std::simd::basic_vec<int128>;

    static_assert(std::is_same_v<decltype(std::simd::partial_load(static_cast<const int128*>(nullptr),
                                                                  std::simd::simd_size_type{})),
                                 int128_default>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const int128*>(nullptr))),
                                 int128_default>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_load(static_cast<const int128*>(nullptr),
                                                                    std::simd::simd_size_type{})),
                                 int128_default>);
#endif

    return 0;
}
