#include "../unique_mapping_probe.hpp"
#include "execution_policy_probe.hpp"
#include <concepts>
#include <cstddef>
#include <type_traits>

namespace {
namespace la = std::linalg;
namespace p = unique_mapping_probe;
using M = p::matrix;
using V = p::vector;
using CV = std::mdspan<const double, p::extents_2>;
using CM = std::mdspan<const double, p::extents_2x2>;

template<class Scalar> concept scaling = requires(Scalar s, V v) { la::scale(s, v); };
template<class Scalar> concept dot_init = requires(Scalar s, V v) { la::dot(v, v, s); };
template<class Scalar> concept rank_scalar = requires(Scalar s, V v, M m) {
    la::symmetric_matrix_rank_1_update(s, v, m, la::upper_triangle);
    la::hermitian_matrix_rank_k_update(s, m, m, la::upper_triangle);
};
struct nonregular { nonregular() = delete; };
static_assert(scaling<double> && dot_init<double> && rank_scalar<double>);
static_assert(!scaling<V> && !dot_init<V> && !rank_scalar<V>);
static_assert(!scaling<nonregular> && !dot_init<nonregular> && !rank_scalar<nonregular>);
#if FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS
static_assert(!scaling<std::execution::sequenced_policy>);
static_assert(!dot_init<std::execution::sequenced_policy>);
static_assert(!rank_scalar<std::execution::sequenced_policy>);
#endif
static_assert(p::all_equal(p::matrix_writes<CM>, false));
static_assert(p::all_equal(p::vector_writes<CV>, false));
static_assert(p::all_equal(p::structured_writes<CM>, false));

template<class X> concept output_solve = requires(M a, CV b, X x) {
    la::triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, b, x);
};
template<class X> concept output_matrix_solve = requires(M a, CM b, X x) {
    la::triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, b, x);
    la::triangular_matrix_matrix_right_solve(a, la::upper_triangle, la::explicit_diagonal, b, x);
};
static_assert(output_solve<V> && !output_solve<CV>);
static_assert(output_matrix_solve<M> && !output_matrix_solve<CM>);

template<class Out> constexpr auto new_vector_writes = std::array{
    requires(M a, CV v, Out o) { la::triangular_matrix_vector_product(a, la::upper_triangle, la::explicit_diagonal, v, v, o); },
    requires(M a, CV v, Out o) { la::triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, v, o, std::divides<void>{}); },
};
template<class Out> constexpr auto new_matrix_writes = std::array{
    requires(M a, CM b, Out o) { la::triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, b, o); },
    requires(M a, CM b, Out o) { la::triangular_matrix_matrix_right_solve(a, la::upper_triangle, la::explicit_diagonal, b, o); },
    requires(M a, Out o) { la::triangular_matrix_product(a, la::upper_triangle, la::implicit_unit_diagonal, a, o); },
    requires(M a, Out o) { la::triangular_matrix_product(a, a, la::upper_triangle, la::implicit_unit_diagonal, o); },
    requires(M a, Out o) { la::triangular_matrix_product(a, la::upper_triangle, la::explicit_diagonal, a, a, o); },
    requires(M a, Out o) { la::triangular_matrix_product(a, a, la::upper_triangle, la::explicit_diagonal, a, o); },
    requires(M a, Out o) { la::symmetric_matrix_product(a, a, la::upper_triangle, o); },
    requires(M a, Out o) { la::symmetric_matrix_product(a, la::upper_triangle, a, a, o); },
    requires(M a, Out o) { la::symmetric_matrix_product(a, a, la::upper_triangle, a, o); },
    requires(M a, Out o) { la::hermitian_matrix_product(a, a, la::upper_triangle, o); },
    requires(M a, Out o) { la::hermitian_matrix_product(a, la::upper_triangle, a, a, o); },
    requires(M a, Out o) { la::hermitian_matrix_product(a, a, la::upper_triangle, a, o); },
};
template<class Out> constexpr auto new_rank_writes = std::array{
    requires(M a, Out o) { la::hermitian_matrix_rank_2k_update(a, a, o, la::upper_triangle); },
    requires(M a, Out o) { la::hermitian_matrix_rank_2k_update(a, a, a, o, la::upper_triangle); },
};
static_assert(p::all_equal(new_vector_writes<V>, true));
static_assert(p::all_equal(new_vector_writes<CV>, false));
static_assert(p::all_equal(new_matrix_writes<M>, true));
static_assert(p::all_equal(new_matrix_writes<CM>, false));
static_assert(p::all_equal(new_matrix_writes<p::upper_matrix>, false));
static_assert(p::all_equal(new_rank_writes<CM>, false));
static_assert(p::all_equal(new_rank_writes<p::upper_matrix>, true));
static_assert(p::all_equal(new_rank_writes<p::lower_matrix>, false));

template<class E> concept rank1_input = requires(V x, E e, M c) {
    la::symmetric_matrix_rank_1_update(1.0, x, e, c, la::upper_triangle);
    la::hermitian_matrix_rank_1_update(1.0, x, e, c, la::upper_triangle);
};
static_assert(rank1_input<p::lower_matrix>);

using S = la::scaled_accessor<double, std::default_accessor<double>>;
using C = la::conjugated_accessor<std::default_accessor<double>>;
static_assert(std::same_as<decltype(&S::access), S::reference (S::*)(double*, std::size_t) const>);
static_assert(std::same_as<decltype(&S::offset), double* (S::*)(double*, std::size_t) const>);
static_assert(std::same_as<decltype(&C::access), C::reference (C::*)(double*, std::size_t) const>);
static_assert(std::same_as<decltype(&C::offset), double* (C::*)(double*, std::size_t) const>);

struct index { constexpr operator int() const noexcept { return 1; } };
struct throwing_index { operator int() const noexcept(false) { return 1; } };
struct explicit_index { explicit operator int() const noexcept { return 1; } };
using Packed = p::upper_layout::mapping<p::extents_2x2>;
template<class I> concept packed_index = requires(Packed m, I i) { m(i, 0); m(0, i); };
static_assert(packed_index<index> && packed_index<short>);
static_assert(!packed_index<throwing_index> && !packed_index<explicit_index>);
static_assert(Packed{}(index{}, 0) == Packed{}(1, 0));
using TE = std::extents<int, 2, 3>;
using DE = std::dextents<int, 2>;
using Transpose = la::layout_transpose<std::layout_right>;
constexpr Transpose::mapping<TE> fixed(std::layout_right::mapping<std::extents<int, 3, 2>>{});
constexpr Transpose::mapping<DE> dynamic(std::layout_right::mapping<DE>(DE(3, 2)));
static_assert(fixed == dynamic && dynamic == fixed);
static_assert(fixed.template operator()<short, index>(0, index{}) == 2);
static_assert(Packed{} == p::upper_layout::mapping<DE>(DE(2, 2)));

// Five-argument (b,x) must beat (b,divide) for mutable and const inputs.
// Instantiating the bodies detects an accidental mdspan-as-divide selection.
void selection() {
    double ad[]{2, 1, 0, 2}, bd[]{6, 4, 6, 4}, xd[4]{};
    M a(ad), b(bd), x(xd);
    V bv(bd), xv(xd);
    auto divide = [](double n, double d) { return n / d; };
    la::triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, bv, xv);
    la::triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, CV(bv), xv);
    la::triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, bv, divide);
    la::triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, bv, xv, divide);
    la::triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, b, x);
    la::triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, CM(b), x);
    la::triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, b, divide);
    la::triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, b, x, divide);
    la::triangular_matrix_matrix_right_solve(a, la::upper_triangle, la::explicit_diagonal, b, x);
    la::triangular_matrix_matrix_right_solve(a, la::upper_triangle, la::explicit_diagonal, CM(b), x);
    la::triangular_matrix_matrix_right_solve(a, la::upper_triangle, la::explicit_diagonal, b, divide);
    la::triangular_matrix_matrix_right_solve(a, la::upper_triangle, la::explicit_diagonal, b, x, divide);
#if FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS
    const auto& policy = std::execution::seq;
    la::triangular_matrix_vector_solve(policy, a, la::upper_triangle, la::explicit_diagonal, bv, xv);
    la::triangular_matrix_vector_solve(policy, a, la::upper_triangle, la::explicit_diagonal, CV(bv), xv);
    la::triangular_matrix_vector_solve(policy, a, la::upper_triangle, la::explicit_diagonal, bv, divide);
    la::triangular_matrix_vector_solve(policy, a, la::upper_triangle, la::explicit_diagonal, bv, xv, divide);
    la::triangular_matrix_matrix_left_solve(policy, a, la::upper_triangle, la::explicit_diagonal, b, x);
    la::triangular_matrix_matrix_left_solve(policy, a, la::upper_triangle, la::explicit_diagonal, CM(b), x);
    la::triangular_matrix_matrix_left_solve(policy, a, la::upper_triangle, la::explicit_diagonal, b, divide);
    la::triangular_matrix_matrix_left_solve(policy, a, la::upper_triangle, la::explicit_diagonal, b, x, divide);
    la::triangular_matrix_matrix_right_solve(policy, a, la::upper_triangle, la::explicit_diagonal, b, x);
    la::triangular_matrix_matrix_right_solve(policy, a, la::upper_triangle, la::explicit_diagonal, CM(b), x);
    la::triangular_matrix_matrix_right_solve(policy, a, la::upper_triangle, la::explicit_diagonal, b, divide);
    la::triangular_matrix_matrix_right_solve(policy, a, la::upper_triangle, la::explicit_diagonal, b, x, divide);
#endif
}
} // namespace
int main() { selection(); }
