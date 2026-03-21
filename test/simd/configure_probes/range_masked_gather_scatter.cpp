#include <simd>

#include <complex>
#include <span>
#include <type_traits>

int main() {
    using int4 = std::simd::vec<int, 4>;
    using complex4f = std::simd::vec<std::complex<float>, 4>;
    using mask4 = std::simd::mask<int, 4>;

    static_assert(std::is_same_v<decltype(std::simd::partial_gather_from(std::declval<std::span<const int, 8>>(),
                                                                         mask4{},
                                                                         int4{})),
                                 int4>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_gather_from(std::declval<std::span<const int, 8>>(),
                                                                           mask4{},
                                                                           int4{},
                                                                           std::simd::flag_default)),
                                 int4>);
    static_assert(std::is_same_v<decltype(std::simd::partial_scatter_to(std::declval<const int4&>(),
                                                                        std::declval<std::span<int, 8>>(),
                                                                        mask4{},
                                                                        int4{})),
                                 void>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_scatter_to(std::declval<const int4&>(),
                                                                          std::declval<std::span<int, 8>>(),
                                                                          mask4{},
                                                                          int4{},
                                                                          std::simd::flag_default)),
                                 void>);

    static_assert(std::is_same_v<decltype(std::simd::partial_gather_from(std::declval<std::span<const std::complex<float>, 8>>(),
                                                                         int4{})),
                                 complex4f>);
    static_assert(std::is_same_v<decltype(std::simd::partial_gather_from(std::declval<std::span<const std::complex<float>, 8>>(),
                                                                         mask4{},
                                                                         int4{})),
                                 complex4f>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_gather_from(std::declval<std::span<const std::complex<float>, 8>>(),
                                                                           int4{},
                                                                           std::simd::flag_default)),
                                 complex4f>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_gather_from(std::declval<std::span<const std::complex<float>, 8>>(),
                                                                           mask4{},
                                                                           int4{},
                                                                           std::simd::flag_default)),
                                 complex4f>);

    return 0;
}
