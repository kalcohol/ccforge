#pragma once

namespace detail::special_math {

template<class T>
T comp_ellint_1(T x) {
#ifdef __cpp_lib_math_special_functions
    return std::comp_ellint_1(x);
#else
    return comp_ellint_1_fallback(x);
#endif
}

template<class T>
T comp_ellint_2(T x) {
#ifdef __cpp_lib_math_special_functions
    return std::comp_ellint_2(x);
#else
    return comp_ellint_2_fallback(x);
#endif
}

template<class T>
T expint(T x) {
#ifdef __cpp_lib_math_special_functions
    return std::expint(x);
#else
    return expint_fallback(x);
#endif
}

template<class T>
T riemann_zeta(T x) {
#ifdef __cpp_lib_math_special_functions
    return std::riemann_zeta(x);
#else
    return riemann_zeta_fallback(x);
#endif
}

template<class T>
T beta(T x, T y) {
#ifdef __cpp_lib_math_special_functions
    return std::beta(x, y);
#else
    return beta_fallback(x, y);
#endif
}

template<class T>
T comp_ellint_3(T k, T nu) {
#ifdef __cpp_lib_math_special_functions
    return std::comp_ellint_3(k, nu);
#else
    return comp_ellint_3_fallback(k, nu);
#endif
}

template<class T>
T cyl_bessel_i(T nu, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::cyl_bessel_i(nu, x);
#else
    return cyl_bessel_i_series(nu, x);
#endif
}

template<class T>
T cyl_bessel_j(T nu, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::cyl_bessel_j(nu, x);
#else
    return cyl_bessel_j_fallback(nu, x);
#endif
}

template<class T>
T cyl_bessel_k(T nu, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::cyl_bessel_k(nu, x);
#else
    return cyl_bessel_k_fallback(nu, x);
#endif
}

template<class T>
T cyl_neumann(T nu, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::cyl_neumann(nu, x);
#else
    return cyl_bessel_y_fallback(nu, x);
#endif
}

template<class T>
T ellint_1(T k, T phi) {
#ifdef __cpp_lib_math_special_functions
    return std::ellint_1(k, phi);
#else
    return ellint_1_fallback(k, phi);
#endif
}

template<class T>
T ellint_2(T k, T phi) {
#ifdef __cpp_lib_math_special_functions
    return std::ellint_2(k, phi);
#else
    return ellint_2_fallback(k, phi);
#endif
}

template<class T>
T ellint_3(T k, T nu, T phi) {
#ifdef __cpp_lib_math_special_functions
    return std::ellint_3(k, nu, phi);
#else
    return ellint_3_fallback(k, nu, phi);
#endif
}

template<class T>
T hermite(unsigned n, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::hermite(n, x);
#else
    return hermite_fallback(n, x);
#endif
}

template<class T>
T laguerre(unsigned n, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::laguerre(n, x);
#else
    return laguerre_fallback(n, x);
#endif
}

template<class T>
T legendre(unsigned n, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::legendre(n, x);
#else
    return legendre_fallback(n, x);
#endif
}

template<class T>
T sph_bessel(unsigned n, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::sph_bessel(n, x);
#else
    return sph_bessel_fallback(n, x);
#endif
}

template<class T>
T sph_neumann(unsigned n, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::sph_neumann(n, x);
#else
    return sph_neumann_fallback(n, x);
#endif
}

template<class T>
T assoc_laguerre(unsigned n, unsigned m, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::assoc_laguerre(n, m, x);
#else
    return assoc_laguerre_fallback(n, m, x);
#endif
}

template<class T>
T assoc_legendre(unsigned l, unsigned m, T x) {
#ifdef __cpp_lib_math_special_functions
    return std::assoc_legendre(l, m, x);
#else
    return assoc_legendre_fallback(l, m, x);
#endif
}

template<class T>
T sph_legendre(unsigned l, unsigned m, T theta) {
#ifdef __cpp_lib_math_special_functions
    return std::sph_legendre(l, m, theta);
#else
    return sph_legendre_fallback(l, m, theta);
#endif
}

} // namespace detail::special_math
