#include <cmath>

#ifndef __cpp_lib_math_special_functions
#error "__cpp_lib_math_special_functions is required"
#endif

int main() {
    const double x = 0.2;
    const double y = 0.3;
    const double z = 0.4;
    const unsigned order = 1u;
    const unsigned degree = 0u;

    const auto value =
        std::comp_ellint_1(x) +
        std::comp_ellint_2(x) +
        std::expint(x) +
        std::riemann_zeta(2.0) +
        std::beta(x, y) +
        std::comp_ellint_3(x, y) +
        std::cyl_bessel_i(x, y) +
        std::cyl_bessel_j(x, y) +
        std::cyl_bessel_k(x, y) +
        std::cyl_neumann(x, y) +
        std::ellint_1(x, y) +
        std::ellint_2(x, y) +
        std::ellint_3(x, y, z) +
        std::hermite(order, x) +
        std::laguerre(order, x) +
        std::legendre(order, x) +
        std::sph_bessel(order, x) +
        std::sph_neumann(order, x) +
        std::assoc_laguerre(order, degree, x) +
        std::assoc_legendre(order, degree, x) +
        std::sph_legendre(order, degree, x);

    return value > 0.0 ? 0 : 1;
}
