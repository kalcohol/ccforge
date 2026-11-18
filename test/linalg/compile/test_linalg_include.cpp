// Compile probe: transparent <linalg> include and representative public names.

#include <linalg>
#include <complex>
#include <mdspan>
#include <type_traits>

#if !defined(FORGE_BACKPORT_LINALG_HPP_INCLUDED) && __has_include(<execution>)
#include <execution>
#endif
#include <forge/linalg.hpp>

#if (defined(FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS) && \
     FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS) || \
    (!defined(FORGE_BACKPORT_LINALG_HPP_INCLUDED) && \
     defined(__cpp_lib_execution) && __cpp_lib_execution >= 201603L)
#define FORGE_TEST_LINALG_POLICIES 1
static void check_policy_calls()
{
    const auto check = [](auto&& policy) {
#if !defined(FORGE_LINALG_BACKPORT_ACTIVE)
        double x_data[] = {3.0, 4.0};
        std::mdspan x(x_data, std::extents<int, 2>{});
        double y_data[] = {1.0, 2.0};
        std::mdspan y(y_data, std::extents<int, 2>{});
        std::mdspan matrix(x_data, std::extents<int, 1, 2>{});
        if constexpr (requires { std::linalg::vector_two_norm(policy, x); })
            (void)forge::vector_two_norm(policy, x);
        if constexpr (requires { std::linalg::vector_two_norm(policy, x, 0.0); })
            (void)forge::vector_two_norm(policy, x, 0.0);
        if constexpr (requires { std::linalg::matrix_frob_norm(policy, matrix); })
            (void)forge::matrix_frob_norm(policy, matrix);
        if constexpr (requires { std::linalg::matrix_frob_norm(policy, matrix, 0.0); })
            (void)forge::matrix_frob_norm(policy, matrix, 0.0);
        if constexpr (requires { std::linalg::apply_givens_rotation(policy, x, y, 0.6, 0.8); })
            forge::apply_givens_rotation(policy, x, y, 0.6, 0.8);
#endif
        std::complex<double> complex_data[] = {{3.0, 0.0}, {0.0, 4.0}};
        std::mdspan cx(complex_data, std::extents<int, 1>{});
        std::mdspan cy(complex_data + 1, std::extents<int, 1>{});
        if constexpr (requires { std::linalg::apply_givens_rotation(policy, cx, cy, 0.6, {0.0, -0.8}); })
            std::linalg::apply_givens_rotation(policy, cx, cy, 0.6, {0.0, -0.8});
    };
    check(std::execution::seq);
    check(std::execution::par);
    check(std::execution::par_unseq);
#if __cpp_lib_execution >= 201902L
    check(std::execution::unseq);
#endif
}
#endif

static_assert(std::is_same_v<decltype(std::linalg::upper_triangle),
                             const std::linalg::upper_triangle_t>);

#if defined(FORGE_LINALG_BACKPORT_ACTIVE)
static void check_explicit_forge_templates()
{
    using vector_extents = std::extents<int, 1>;
    using matrix_extents = std::extents<int, 1, 1>;
    using layout = std::layout_right;
    using complex = std::complex<double>;
    double real_data[]{3.0};
    complex complex_data[]{{3.0, 4.0}};
    const std::mdspan<double, vector_extents> v(real_data);
    const std::mdspan<complex, matrix_extents> m(complex_data);

    (void)forge::vector_two_norm<vector_extents>(v);
    (void)forge::vector_two_norm<vector_extents, layout, std::default_accessor<double>>(v);
    (void)forge::vector_two_norm<vector_extents, layout, std::default_accessor<double>, double>(v, 0.0);
    (void)forge::matrix_frob_norm<matrix_extents>(m);
    (void)forge::matrix_frob_norm<matrix_extents, layout, std::default_accessor<complex>>(m);
    (void)forge::matrix_frob_norm<matrix_extents, layout, std::default_accessor<complex>, complex>(m, {});

    const auto rotation = forge::setup_givens_rotation<complex>(complex{3.0}, {0.0, 4.0});
    static_assert(std::is_same_v<decltype(rotation.c), double>);
    std::mdspan<double, vector_extents> x(real_data);
    double other[]{4.0};
    std::mdspan<double, vector_extents> y(other);
    using accessor = std::default_accessor<double>;
    forge::apply_givens_rotation<vector_extents, layout, accessor, vector_extents, layout, accessor>(
        x, y, rotation.c, 0.0);
}
#endif

static void check_include_surface()
{
    double data[] = {3.0, 4.0};
    std::mdspan v(data, std::extents<int, 2>{});
    auto rotation = std::linalg::setup_givens_rotation(3.0, 4.0);
    (void)std::linalg::vector_two_norm(v);
    std::linalg::apply_givens_rotation(v, v, rotation.c, rotation.s);

    using complex = std::complex<double>;
    const auto std_rotation = std::linalg::setup_givens_rotation(complex{3.0}, {0.0, 4.0});
    complex complex_data[] = {{3.0, 0.0}, {0.0, 4.0}};
    std::mdspan x(complex_data, std::extents<int, 1>{});
    std::mdspan y(complex_data + 1, std::extents<int, 1>{});
    std::linalg::apply_givens_rotation(x, y, std_rotation.c, {0.0, -0.8});
#if defined(FORGE_LINALG_BACKPORT_ACTIVE)
    const auto forge_rotation = forge::setup_givens_rotation({3.0, 0.0}, complex{0.0, 4.0});
    forge::apply_givens_rotation(x, y, forge_rotation.c, {0.0, -0.8});
#endif
}

int main()
{
    check_include_surface();
#if defined(FORGE_LINALG_BACKPORT_ACTIVE)
    check_explicit_forge_templates();
#endif
#if defined(FORGE_TEST_LINALG_POLICIES)
    check_policy_calls();
#endif
}
