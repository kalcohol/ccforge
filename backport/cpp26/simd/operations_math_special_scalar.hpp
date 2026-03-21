namespace detail::special_math {

template<class T>
constexpr T euler_gamma_v = static_cast<T>(0.57721566490153286060651209008240243104L);

template<class T>
constexpr T pi_v = static_cast<T>(3.141592653589793238462643383279502884L);

template<class T>
T quiet_nan() noexcept {
    if constexpr (numeric_limits<T>::has_quiet_NaN) {
        return numeric_limits<T>::quiet_NaN();
    } else {
        return T{};
    }
}

template<class T>
T infinity() noexcept {
    if constexpr (numeric_limits<T>::has_infinity) {
        return numeric_limits<T>::infinity();
    } else {
        return numeric_limits<T>::max();
    }
}

template<class T>
bool almost_equal(T left, T right, T tolerance = static_cast<T>(1e-12L)) noexcept {
    return std::abs(left - right) <= tolerance;
}

template<class T>
bool almost_integer(T value, T tolerance = static_cast<T>(1e-12L)) noexcept {
    return almost_equal(value, std::round(value), tolerance);
}

template<class Fun>
long double simpson_integral(Fun&& fun, long double a, long double b) {
    const long double mid = (a + b) / 2.0L;
    return (b - a) * (fun(a) + 4.0L * fun(mid) + fun(b)) / 6.0L;
}

template<class Fun>
long double adaptive_simpson_integral(Fun&& fun,
                                      long double a,
                                      long double b,
                                      long double epsilon,
                                      long double whole,
                                      int depth) {
    const long double mid = (a + b) / 2.0L;
    const long double left = simpson_integral(fun, a, mid);
    const long double right = simpson_integral(fun, mid, b);
    const long double delta = left + right - whole;

    if (depth <= 0 || std::abs(delta) <= 15.0L * epsilon) {
        return left + right + delta / 15.0L;
    }

    return adaptive_simpson_integral(fun, a, mid, epsilon / 2.0L, left, depth - 1) +
        adaptive_simpson_integral(fun, mid, b, epsilon / 2.0L, right, depth - 1);
}

template<class Fun>
long double adaptive_simpson_integral(Fun&& fun, long double a, long double b) {
    if (a == b) {
        return 0.0L;
    }

    const long double whole = simpson_integral(fun, a, b);
    return adaptive_simpson_integral(fun, a, b, 1e-12L, whole, 18);
}

template<class T>
T beta_fallback(T x, T y) {
    if (!(x > T{}) || !(y > T{})) {
        return quiet_nan<T>();
    }

    return std::exp(std::lgamma(x) + std::lgamma(y) - std::lgamma(x + y));
}

template<class T>
T hermite_fallback(unsigned n, T x) {
    if (n == 0u) {
        return T{1};
    }
    if (n == 1u) {
        return static_cast<T>(2) * x;
    }

    T hm2 = T{1};
    T hm1 = static_cast<T>(2) * x;
    for (unsigned i = 1; i < n; ++i) {
        const T next = static_cast<T>(2) * x * hm1 - static_cast<T>(2 * i) * hm2;
        hm2 = hm1;
        hm1 = next;
    }
    return hm1;
}

template<class T>
T laguerre_fallback(unsigned n, T x) {
    if (n == 0u) {
        return T{1};
    }
    if (n == 1u) {
        return T{1} - x;
    }

    T lm2 = T{1};
    T lm1 = T{1} - x;
    for (unsigned i = 1; i < n; ++i) {
        const T next = ((static_cast<T>(2 * i + 1) - x) * lm1 - static_cast<T>(i) * lm2) / static_cast<T>(i + 1);
        lm2 = lm1;
        lm1 = next;
    }
    return lm1;
}

template<class T>
T legendre_fallback(unsigned n, T x) {
    if (std::abs(x) > T{1}) {
        return quiet_nan<T>();
    }
    if (n == 0u) {
        return T{1};
    }
    if (n == 1u) {
        return x;
    }

    T pm2 = T{1};
    T pm1 = x;
    for (unsigned i = 1; i < n; ++i) {
        const T next = (static_cast<T>(2 * i + 1) * x * pm1 - static_cast<T>(i) * pm2) / static_cast<T>(i + 1);
        pm2 = pm1;
        pm1 = next;
    }
    return pm1;
}

template<class T>
T assoc_laguerre_fallback(unsigned n, unsigned m, T x) {
    if (n == 0u) {
        return T{1};
    }
    if (n == 1u) {
        return static_cast<T>(m + 1) - x;
    }

    T lm2 = T{1};
    T lm1 = static_cast<T>(m + 1) - x;
    for (unsigned i = 1; i < n; ++i) {
        const T next =
            ((static_cast<T>(2 * i + m + 1) - x) * lm1 - static_cast<T>(i + m) * lm2) / static_cast<T>(i + 1);
        lm2 = lm1;
        lm1 = next;
    }
    return lm1;
}

template<class T>
T assoc_legendre_fallback(unsigned l, unsigned m, T x) {
    if (m > l || std::abs(x) > T{1}) {
        return quiet_nan<T>();
    }

    T pmm = T{1};
    if (m > 0u) {
        const T one_minus_x2 = std::max(T{}, T{1} - x * x);
        const T root = std::sqrt(one_minus_x2);
        T factor = T{1};
        for (unsigned i = 1; i <= m; ++i) {
            pmm *= factor * root;
            factor += T{2};
        }
    }

    if (l == m) {
        return pmm;
    }

    T pmmp1 = static_cast<T>(2 * m + 1) * x * pmm;
    if (l == m + 1u) {
        return pmmp1;
    }

    T pll = T{};
    for (unsigned ll = m + 2u; ll <= l; ++ll) {
        pll = (static_cast<T>(2 * ll - 1) * x * pmmp1 - static_cast<T>(ll + m - 1) * pmm) /
            static_cast<T>(ll - m);
        pmm = pmmp1;
        pmmp1 = pll;
    }
    return pll;
}

template<class T>
T sph_legendre_fallback(unsigned l, unsigned m, T theta) {
    if (m > l) {
        return quiet_nan<T>();
    }

    const T cos_theta = std::cos(theta);
    const T assoc = assoc_legendre_fallback<T>(l, m, cos_theta);
    const T log_ratio = std::lgamma(static_cast<T>(l - m + 1)) - std::lgamma(static_cast<T>(l + m + 1));
    const T norm = std::exp((std::log((static_cast<T>(2 * l + 1)) / (static_cast<T>(4) * pi_v<T>)) + log_ratio) / 2);
    const T phase = (m % 2u == 0u) ? T{1} : T{-1};
    return phase * norm * assoc;
}

template<class T>
T sph_bessel_fallback(unsigned n, T x) {
    if (x == T{}) {
        return n == 0u ? T{1} : T{};
    }

    const T j0 = std::sin(x) / x;
    if (n == 0u) {
        return j0;
    }

    T jm2 = j0;
    T jm1 = std::sin(x) / (x * x) - std::cos(x) / x;
    if (n == 1u) {
        return jm1;
    }

    for (unsigned i = 1; i < n; ++i) {
        const T next = (static_cast<T>(2 * i + 1) / x) * jm1 - jm2;
        jm2 = jm1;
        jm1 = next;
    }
    return jm1;
}

template<class T>
T sph_neumann_fallback(unsigned n, T x) {
    if (x == T{}) {
        return -infinity<T>();
    }

    const T y0 = -std::cos(x) / x;
    if (n == 0u) {
        return y0;
    }

    T ym2 = y0;
    T ym1 = -std::cos(x) / (x * x) - std::sin(x) / x;
    if (n == 1u) {
        return ym1;
    }

    for (unsigned i = 1; i < n; ++i) {
        const T next = (static_cast<T>(2 * i + 1) / x) * ym1 - ym2;
        ym2 = ym1;
        ym1 = next;
    }
    return ym1;
}

template<class T>
T expint_fallback(T x) {
    if (x == T{}) {
        return -infinity<T>();
    }

    constexpr T series_cutoff = static_cast<T>(6);
    constexpr T epsilon = static_cast<T>(1e-15L);

    if (std::abs(x) <= series_cutoff) {
        T sum = T{};
        T term = x;
        for (unsigned k = 1; k < 512u; ++k) {
            const T add = term / static_cast<T>(k);
            sum += add;
            term *= x / static_cast<T>(k + 1u);
            if (std::abs(add) <= epsilon * std::max(T{1}, std::abs(sum))) {
                break;
            }
        }
        return euler_gamma_v<T> + std::log(std::abs(x)) + sum;
    }

    T sum = T{1};
    T term = T{1};
    for (unsigned k = 1; k < 256u; ++k) {
        term *= static_cast<T>(k) / x;
        sum += term;
        if (std::abs(term) <= epsilon * std::max(T{1}, std::abs(sum))) {
            break;
        }
    }
    return std::exp(x) * sum / x;
}

template<class T>
T riemann_zeta_hasse(T s) {
    T sum = T{};
    for (unsigned n = 0; n < 128u; ++n) {
        T inner = T{};
        T binom = T{1};
        for (unsigned k = 0; k <= n; ++k) {
            if (k > 0u) {
                binom *= static_cast<T>(n - k + 1u) / static_cast<T>(k);
            }
            const T term = binom / std::pow(static_cast<T>(k + 1u), s);
            inner += (k % 2u == 0u) ? term : -term;
        }

        const T outer = inner / std::ldexp(T{1}, static_cast<int>(n + 1u));
        sum += outer;
        if (std::abs(outer) <= static_cast<T>(1e-15L) * std::max(T{1}, std::abs(sum))) {
            break;
        }
    }

    return sum / (T{1} - std::pow(T{2}, T{1} - s));
}

template<class T>
T riemann_zeta_fallback(T s) {
    if (almost_equal(s, T{1})) {
        return infinity<T>();
    }
    if (almost_equal(s, T{0})) {
        return static_cast<T>(-0.5L);
    }
    if (s < T{}) {
        const T rounded = std::round(s);
        if (almost_equal(s, rounded) && static_cast<long long>(rounded) % 2ll == 0ll) {
            return T{};
        }
        return std::pow(T{2}, s) * std::pow(pi_v<T>, s - T{1}) * std::sin(pi_v<T> * s / T{2}) *
            std::tgamma(T{1} - s) * riemann_zeta_hasse(T{1} - s);
    }

    return riemann_zeta_hasse(s);
}

template<class T, class Fun>
T elliptic_integral(T upper, Fun&& integrand) {
    if (upper == T{}) {
        return T{};
    }

    const T sign = upper < T{} ? T{-1} : T{1};
    const long double bound = std::abs(static_cast<long double>(upper));
    const long double integral = adaptive_simpson_integral(
        [&](long double theta) {
            return static_cast<long double>(integrand(static_cast<T>(theta)));
        },
        0.0L,
        bound);
    return sign * static_cast<T>(integral);
}

template<class T>
T ellint_1_fallback(T k, T phi) {
    return elliptic_integral(phi, [&](T theta) {
        const T s = std::sin(theta);
        const T radicand = T{1} - k * k * s * s;
        if (radicand <= T{}) {
            return infinity<T>();
        }
        return T{1} / std::sqrt(radicand);
    });
}

template<class T>
T ellint_2_fallback(T k, T phi) {
    return elliptic_integral(phi, [&](T theta) {
        const T s = std::sin(theta);
        const T radicand = T{1} - k * k * s * s;
        if (radicand < T{}) {
            return quiet_nan<T>();
        }
        return std::sqrt(radicand);
    });
}

template<class T>
T ellint_3_fallback(T k, T nu, T phi) {
    return elliptic_integral(phi, [&](T theta) {
        const T s = std::sin(theta);
        const T sin2 = s * s;
        const T radicand = T{1} - k * k * sin2;
        const T pole = T{1} - nu * sin2;
        if (radicand <= T{} || pole == T{}) {
            return infinity<T>();
        }
        return T{1} / (pole * std::sqrt(radicand));
    });
}

template<class T>
T comp_ellint_1_fallback(T k) {
    return ellint_1_fallback(k, pi_v<T> / T{2});
}

template<class T>
T comp_ellint_2_fallback(T k) {
    return ellint_2_fallback(k, pi_v<T> / T{2});
}

template<class T>
T comp_ellint_3_fallback(T k, T nu) {
    return ellint_3_fallback(k, nu, pi_v<T> / T{2});
}

template<class T>
T cyl_bessel_j_series(T nu, T x) {
    if (x < T{}) {
        return quiet_nan<T>();
    }
    if (x == T{}) {
        return almost_equal(nu, T{}) ? T{1} : T{};
    }

    const T half_x = x / T{2};
    T term = std::pow(half_x, nu) / std::tgamma(nu + T{1});
    T sum = term;
    for (unsigned k = 0; k < 512u; ++k) {
        term *= -(half_x * half_x) / (static_cast<T>(k + 1u) * (nu + static_cast<T>(k + 1u)));
        sum += term;
        if (std::abs(term) <= static_cast<T>(1e-15L) * std::max(T{1}, std::abs(sum))) {
            break;
        }
    }
    return sum;
}

template<class T>
T cyl_bessel_i_series(T nu, T x) {
    if (x < T{}) {
        return quiet_nan<T>();
    }
    if (x == T{}) {
        return almost_equal(nu, T{}) ? T{1} : T{};
    }

    const T half_x = x / T{2};
    T term = std::pow(half_x, nu) / std::tgamma(nu + T{1});
    T sum = term;
    for (unsigned k = 0; k < 512u; ++k) {
        term *= (half_x * half_x) / (static_cast<T>(k + 1u) * (nu + static_cast<T>(k + 1u)));
        sum += term;
        if (std::abs(term) <= static_cast<T>(1e-15L) * std::max(T{1}, std::abs(sum))) {
            break;
        }
    }
    return sum;
}

template<class T>
T cyl_bessel_y_fallback(T nu, T x) {
    if (!(x > T{})) {
        return quiet_nan<T>();
    }

    T adjusted_nu = nu;
    if (almost_integer(adjusted_nu, static_cast<T>(1e-10L))) {
        adjusted_nu += static_cast<T>(1e-7L);
    }

    const T sin_term = std::sin(pi_v<T> * adjusted_nu);
    return (cyl_bessel_j_series(adjusted_nu, x) * std::cos(pi_v<T> * adjusted_nu) -
            cyl_bessel_j_series(-adjusted_nu, x)) /
        sin_term;
}

template<class T>
T cyl_bessel_k_fallback(T nu, T x) {
    if (!(x > T{})) {
        return quiet_nan<T>();
    }

    T adjusted_nu = nu;
    if (almost_integer(adjusted_nu, static_cast<T>(1e-10L))) {
        adjusted_nu += static_cast<T>(1e-7L);
    }

    const T sin_term = std::sin(pi_v<T> * adjusted_nu);
    return (pi_v<T> / T{2}) * (cyl_bessel_i_series(-adjusted_nu, x) - cyl_bessel_i_series(adjusted_nu, x)) / sin_term;
}

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
    return cyl_bessel_j_series(nu, x);
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
