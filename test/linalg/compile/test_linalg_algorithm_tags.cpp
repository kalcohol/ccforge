#include <linalg>

#include <array>
#include <mdspan>

namespace {

namespace la = std::linalg;

using matrix = std::mdspan<double, std::extents<int, 2, 2>>;
using vector = std::mdspan<double, std::extents<int, 2>>;
using upper = la::upper_triangle_t;
using lower = la::lower_triangle_t;
using explicit_diag = la::explicit_diagonal_t;
using unit_diag = la::implicit_unit_diagonal_t;

struct derived_triangle : upper {};
struct convertible_triangle {
    operator upper() const { return upper{}; }
};
struct derived_diagonal : unit_diag {};
struct convertible_diagonal {
    operator unit_diag() const { return unit_diag{}; }
};

template<std::size_t N>
consteval bool all_equal(const std::array<bool, N>& calls, bool expected) {
    for (bool call : calls) {
        if (call != expected) return false;
    }
    return true;
}

template<class Triangle, class Diagonal>
constexpr auto triangular_calls = std::array{
    requires(matrix a, vector x, Triangle t, Diagonal d) {
        la::triangular_matrix_vector_product(a, t, d, x);
    },
    requires(matrix a, vector x, vector y, Triangle t, Diagonal d) {
        la::triangular_matrix_vector_product(a, t, d, x, y);
    },
    requires(matrix a, vector x, Triangle t, Diagonal d) {
        la::triangular_matrix_vector_solve(a, t, d, x);
    },
    requires(matrix a, matrix c, Triangle t, Diagonal d) {
        la::triangular_matrix_left_product(a, t, d, c);
    },
    requires(matrix a, matrix c, Triangle t, Diagonal d) {
        la::triangular_matrix_right_product(a, t, d, c);
    },
    requires(matrix a, matrix b, Triangle t, Diagonal d) {
        la::triangular_matrix_matrix_left_solve(a, t, d, b);
    },
};

template<class Triangle>
constexpr auto symmetric_hermitian_calls = std::array{
    requires(matrix a, vector x, vector y, Triangle t) {
        la::symmetric_matrix_vector_product(a, t, x, y);
    },
    requires(matrix a, vector x, vector y, vector z, Triangle t) {
        la::symmetric_matrix_vector_product(a, t, x, y, z);
    },
    requires(matrix a, vector x, vector y, Triangle t) {
        la::hermitian_matrix_vector_product(a, t, x, y);
    },
    requires(matrix a, vector x, vector y, vector z, Triangle t) {
        la::hermitian_matrix_vector_product(a, t, x, y, z);
    },
    requires(vector x, matrix a, Triangle t) {
        la::symmetric_matrix_rank_1_update(1.0, x, a, t);
    },
    requires(vector x, matrix e, matrix a, Triangle t) {
        la::symmetric_matrix_rank_1_update(1.0, x, e, a, t);
    },
    requires(vector x, matrix a, Triangle t) {
        la::symmetric_matrix_rank_1_update(1.0, t, x, a);
    },
    requires(vector x, matrix a, Triangle t) {
        la::hermitian_matrix_rank_1_update(1.0, x, a, t);
    },
    requires(vector x, matrix e, matrix a, Triangle t) {
        la::hermitian_matrix_rank_1_update(1.0, x, e, a, t);
    },
    requires(vector x, matrix a, Triangle t) {
        la::hermitian_matrix_rank_1_update(t, x, a);
    },
    requires(vector x, vector y, matrix a, Triangle t) {
        la::symmetric_matrix_rank_2_update(x, y, a, t);
    },
    requires(vector x, vector y, matrix e, matrix a, Triangle t) {
        la::symmetric_matrix_rank_2_update(x, y, e, a, t);
    },
    requires(vector x, vector y, matrix a, Triangle t) {
        la::symmetric_matrix_rank_2_update(1.0, t, x, y, a);
    },
    requires(vector x, vector y, matrix a, Triangle t) {
        la::hermitian_matrix_rank_2_update(x, y, a, t);
    },
    requires(vector x, vector y, matrix e, matrix a, Triangle t) {
        la::hermitian_matrix_rank_2_update(x, y, e, a, t);
    },
    requires(vector x, vector y, matrix a, Triangle t) {
        la::hermitian_matrix_rank_2_update(t, x, y, a);
    },
    requires(matrix a, matrix b, matrix c, Triangle t) {
        la::symmetric_matrix_product(a, t, b, c);
    },
    requires(matrix a, matrix b, matrix c, Triangle t) {
        la::hermitian_matrix_product(a, t, b, c);
    },
    requires(matrix a, matrix b, matrix c, Triangle t) {
        la::hermitian_matrix_product(t, a, b, c);
    },
    requires(matrix a, matrix c, Triangle t) {
        la::symmetric_matrix_rank_k_update(1.0, a, c, t);
    },
    requires(matrix a, matrix e, matrix c, Triangle t) {
        la::symmetric_matrix_rank_k_update(1.0, a, e, c, t);
    },
    requires(matrix a, matrix c, Triangle t) {
        la::symmetric_matrix_rank_k_update(1.0, t, a, c);
    },
    requires(matrix a, matrix c, Triangle t) {
        la::hermitian_matrix_rank_k_update(1.0, a, c, t);
    },
    requires(matrix a, matrix e, matrix c, Triangle t) {
        la::hermitian_matrix_rank_k_update(1.0, a, e, c, t);
    },
    requires(matrix a, matrix c, Triangle t) {
        la::hermitian_matrix_rank_k_update(t, a, c);
    },
    requires(matrix a, matrix b, matrix c, Triangle t) {
        la::symmetric_matrix_rank_2k_update(a, b, c, t);
    },
    requires(matrix a, matrix b, matrix e, matrix c, Triangle t) {
        la::symmetric_matrix_rank_2k_update(a, b, e, c, t);
    },
    requires(matrix a, matrix b, matrix c, Triangle t) {
        la::symmetric_matrix_rank_2k_update(1.0, t, a, b, c);
    },
};

static_assert(all_equal(triangular_calls<upper, explicit_diag>, true));
static_assert(all_equal(triangular_calls<upper, unit_diag>, true));
static_assert(all_equal(triangular_calls<lower, explicit_diag>, true));
static_assert(all_equal(triangular_calls<lower, unit_diag>, true));
static_assert(all_equal(triangular_calls<const upper&, const unit_diag&>, true));
static_assert(all_equal(triangular_calls<const lower&, const explicit_diag&>, true));

static_assert(all_equal(triangular_calls<int, explicit_diag>, false));
static_assert(all_equal(triangular_calls<derived_triangle, unit_diag>, false));
static_assert(all_equal(triangular_calls<convertible_triangle, unit_diag>, false));
static_assert(all_equal(triangular_calls<upper, int>, false));
static_assert(all_equal(triangular_calls<upper, derived_diagonal>, false));
static_assert(all_equal(triangular_calls<upper, convertible_diagonal>, false));
static_assert(all_equal(triangular_calls<explicit_diag, upper>, false));
static_assert(all_equal(triangular_calls<unit_diag, lower>, false));

static_assert(all_equal(symmetric_hermitian_calls<upper>, true));
static_assert(all_equal(symmetric_hermitian_calls<lower>, true));
static_assert(all_equal(symmetric_hermitian_calls<const upper&>, true));
static_assert(all_equal(symmetric_hermitian_calls<int>, false));
static_assert(all_equal(symmetric_hermitian_calls<unit_diag>, false));
static_assert(all_equal(symmetric_hermitian_calls<explicit_diag>, false));
static_assert(all_equal(symmetric_hermitian_calls<derived_triangle>, false));
static_assert(all_equal(symmetric_hermitian_calls<convertible_triangle>, false));

using upper_layout = la::layout_blas_packed<upper, la::row_major_t>;
using lower_layout = la::layout_blas_packed<lower, la::column_major_t>;
using upper_matrix = std::mdspan<double, std::extents<int, 2, 2>, upper_layout>;
using lower_matrix = std::mdspan<double, std::extents<int, 2, 2>, lower_layout>;

template<class Matrix, class Triangle>
concept packed_triangular_input = requires(Matrix a, vector x, Triangle t) {
    la::triangular_matrix_vector_product(a, t, la::explicit_diagonal, x);
};

template<class Matrix, class Triangle>
concept packed_rank_output = requires(Matrix c, matrix a, vector x, Triangle t) {
    la::symmetric_matrix_rank_1_update(1.0, x, c, t);
    la::hermitian_matrix_rank_k_update(1.0, a, c, t);
};

static_assert(packed_triangular_input<upper_matrix, upper>);
static_assert(packed_triangular_input<lower_matrix, lower>);
static_assert(!packed_triangular_input<upper_matrix, lower>);
static_assert(!packed_triangular_input<lower_matrix, upper>);
static_assert(packed_rank_output<upper_matrix, upper>);
static_assert(packed_rank_output<lower_matrix, lower>);
static_assert(!packed_rank_output<upper_matrix, lower>);
static_assert(!packed_rank_output<lower_matrix, upper>);

} // namespace

int main() {}
