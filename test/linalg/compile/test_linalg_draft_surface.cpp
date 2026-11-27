#include <linalg>
#include "native_policy_config.hpp"

#include <complex>
#include <mdspan>
#include <type_traits>

namespace {

namespace la = std::linalg;

template<class T>
using vector = std::mdspan<T, std::extents<int, 2>>;

template<class T>
using matrix = std::mdspan<T, std::extents<int, 2, 2>>;

struct custom_scalar {
    double value{};
    custom_scalar() = default;
    custom_scalar(double v) : value(v) {}
    friend double abs(custom_scalar x) { return x.value; }
};

static_assert(std::is_convertible_v<double, custom_scalar>);

using V = vector<double>;
using M = matrix<double>;
using C = std::complex<double>;
using T = la::upper_triangle_t;

template<class... Args>
concept vector_norm = requires(Args... args) { la::vector_two_norm(args...); };

template<class... Args>
concept matrix_norm = requires(Args... args) { la::matrix_frob_norm(args...); };

template<class Scalar, class... Policy>
concept explicit_vector_init = requires(Policy... policy, V v, Scalar init) {
    la::vector_two_norm<Policy..., V::extents_type, V::layout_type, V::accessor_type, Scalar>(
        policy..., v, init);
};

template<class Scalar, class... Policy>
concept explicit_matrix_init = requires(Policy... policy, M m, Scalar init) {
    la::matrix_frob_norm<Policy..., M::extents_type, M::layout_type, M::accessor_type, Scalar>(
        policy..., m, init);
};

template<class... Args>
concept setup_rotation = requires(Args... args) { la::setup_givens_rotation(args...); };

template<class... Args>
concept apply_rotation = requires(Args... args) { la::apply_givens_rotation(args...); };

template<class Real>
concept explicit_setup_rotation = requires(Real value) {
    la::setup_givens_rotation<Real>(value, value);
};

template<class Real, class... Policy>
concept explicit_apply_rotation = requires(Policy... policy, V x, V y, Real c, Real s) {
    la::apply_givens_rotation<Policy..., V::extents_type, V::layout_type, V::accessor_type,
                              V::extents_type, V::layout_type, V::accessor_type, Real>(
        policy..., x, y, c, s);
};

template<class... Args>
concept symmetric_rank_1 = requires(Args... args) { la::symmetric_matrix_rank_1_update(args...); };

template<class... Args>
concept symmetric_rank_2 = requires(Args... args) { la::symmetric_matrix_rank_2_update(args...); };

template<class... Args>
concept symmetric_rank_k = requires(Args... args) { la::symmetric_matrix_rank_k_update(args...); };

template<class... Args>
concept symmetric_rank_2k = requires(Args... args) { la::symmetric_matrix_rank_2k_update(args...); };

template<class... Args>
concept hermitian_rank_1 = requires(Args... args) { la::hermitian_matrix_rank_1_update(args...); };

template<class... Args>
concept hermitian_rank_2 = requires(Args... args) { la::hermitian_matrix_rank_2_update(args...); };

template<class... Args>
concept hermitian_rank_k = requires(Args... args) { la::hermitian_matrix_rank_k_update(args...); };

template<class... Args>
concept hermitian_product = requires(Args... args) { la::hermitian_matrix_product(args...); };

// Use the same operand types for the sequential and native-policy surfaces.
template<class... Policy>
consteval bool draft_signatures() {
    static_assert(vector_norm<Policy..., V> && vector_norm<Policy..., V, double>);
    static_assert(vector_norm<Policy..., vector<C>> && vector_norm<Policy..., V, C>);
    static_assert(vector_norm<Policy..., vector<std::complex<float>>, double>);
    static_assert(vector_norm<Policy..., vector<float>, C>);
    static_assert(explicit_vector_init<double, Policy...>);
    static_assert(explicit_vector_init<C, Policy...>);
    static_assert(!explicit_vector_init<const double, Policy...>);
    static_assert(!explicit_vector_init<const C, Policy...>);
    static_assert(!vector_norm<Policy..., vector<int>>);
    static_assert(!vector_norm<Policy..., vector<int>, double>);
    static_assert(!vector_norm<Policy..., V, int>);
    static_assert(!vector_norm<Policy..., vector<custom_scalar>>);
    static_assert(!vector_norm<Policy..., vector<custom_scalar>, double>);
    static_assert(!vector_norm<Policy..., V, custom_scalar>);

    static_assert(matrix_norm<Policy..., M> && matrix_norm<Policy..., M, double>);
    static_assert(matrix_norm<Policy..., matrix<C>> && matrix_norm<Policy..., M, C>);
    static_assert(matrix_norm<Policy..., matrix<std::complex<float>>, double>);
    static_assert(matrix_norm<Policy..., matrix<float>, C>);
    static_assert(explicit_matrix_init<double, Policy...>);
    static_assert(explicit_matrix_init<C, Policy...>);
    static_assert(!explicit_matrix_init<const double, Policy...>);
    static_assert(!explicit_matrix_init<const C, Policy...>);
    static_assert(!matrix_norm<Policy..., matrix<int>>);
    static_assert(!matrix_norm<Policy..., matrix<int>, double>);
    static_assert(!matrix_norm<Policy..., M, int>);
    static_assert(!matrix_norm<Policy..., matrix<custom_scalar>>);
    static_assert(!matrix_norm<Policy..., matrix<custom_scalar>, double>);
    static_assert(!matrix_norm<Policy..., M, custom_scalar>);

    static_assert(apply_rotation<Policy..., V, V, double, double>);
    static_assert(apply_rotation<Policy..., vector<C>, vector<C>, double, C>);
    static_assert(!apply_rotation<Policy..., V, V, int, int>);
    static_assert(!apply_rotation<Policy..., V, V, custom_scalar, custom_scalar>);
    static_assert(explicit_apply_rotation<double, Policy...>);
    static_assert(!explicit_apply_rotation<const double, Policy...>);

    static_assert(symmetric_rank_1<Policy..., double, V, M, T>);
    static_assert(!symmetric_rank_1<Policy..., double, T, V, M>);
    static_assert(symmetric_rank_2<Policy..., V, V, M, T>);
    static_assert(!symmetric_rank_2<Policy..., double, T, V, V, M>);
    static_assert(!symmetric_rank_2<Policy..., double, V, V, M, T>);
    static_assert(symmetric_rank_k<Policy..., double, M, M, T>);
    static_assert(!symmetric_rank_k<Policy..., double, T, M, M>);
    static_assert(symmetric_rank_2k<Policy..., M, M, M, T>);
    static_assert(!symmetric_rank_2k<Policy..., double, T, M, M, M>);
    static_assert(!symmetric_rank_2k<Policy..., double, M, M, M, T>);

    static_assert(hermitian_rank_1<Policy..., double, V, M, T>);
    static_assert(!hermitian_rank_1<Policy..., T, V, M>);
    static_assert(hermitian_rank_2<Policy..., V, V, M, T>);
    static_assert(!hermitian_rank_2<Policy..., T, V, V, M>);
    static_assert(hermitian_rank_k<Policy..., double, M, M, T>);
    static_assert(!hermitian_rank_k<Policy..., T, M, M>);
    static_assert(hermitian_product<Policy..., M, T, M, M>);
    static_assert(!hermitian_product<Policy..., T, M, M, M>);
    return true;
}

static_assert(draft_signatures<>());
static_assert(setup_rotation<double, double> && setup_rotation<C, C>);
static_assert(!setup_rotation<int, int> && !setup_rotation<custom_scalar, custom_scalar>);
static_assert(explicit_setup_rotation<double> && !explicit_setup_rotation<const double>);

// The policy assertions below must not disappear silently: the overloads
// exist exactly when the target's policy mode has the native objects.
static_assert(FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS == FORGE_TEST_EXPECT_POLICY_OBJECTS,
    "The linalg policy overloads must follow this target's policy mode");

#if FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS
static_assert(draft_signatures<const std::execution::sequenced_policy&>());
static_assert(!setup_rotation<const std::execution::sequenced_policy&, double, double>);
#endif

} // namespace

int main() {}
