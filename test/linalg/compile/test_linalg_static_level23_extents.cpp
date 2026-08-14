// Compile probe: BLAS-2 and BLAS-3 algorithms reject statically incompatible
// operands while retaining dynamic/static compatible forms.

#include <linalg>
#include <mdspan>

template<class A, class X, class Y>
concept matrix_vector_shapes = requires(A a, X x, Y y) {
    std::linalg::matrix_vector_product(a, x, y);
};

template<class A, class X, class Y, class Z>
concept matrix_vector_update_shapes = requires(A a, X x, Y y, Z z) {
    std::linalg::matrix_vector_product(a, x, y, z);
};

template<class A, class X>
concept triangular_vector_shapes = requires(A a, X x) {
    std::linalg::triangular_matrix_vector_product(
        a, std::linalg::upper_triangle,
        std::linalg::explicit_diagonal, x);
    std::linalg::triangular_matrix_vector_solve(
        a, std::linalg::upper_triangle,
        std::linalg::explicit_diagonal, x);
};

template<class A, class X, class Y>
concept symmetric_vector_shapes = requires(A a, X x, Y y) {
    std::linalg::symmetric_matrix_vector_product(
        a, std::linalg::upper_triangle, x, y);
    std::linalg::hermitian_matrix_vector_product(
        a, std::linalg::upper_triangle, x, y);
};

template<class X, class Y, class A>
concept rank_one_shapes = requires(X x, Y y, A a) {
    std::linalg::matrix_rank_1_update(x, y, a);
    std::linalg::matrix_rank_1_update_c(x, y, a);
};

template<class X, class Y, class A>
concept symmetric_rank_two_shapes = requires(X x, Y y, A a) {
    std::linalg::symmetric_matrix_rank_2_update(
        x, y, a, std::linalg::upper_triangle);
    std::linalg::hermitian_matrix_rank_2_update(
        x, y, a, std::linalg::upper_triangle);
};

template<class A, class B, class C>
concept matrix_product_shapes = requires(A a, B b, C c) {
    std::linalg::matrix_product(a, b, c);
};

template<class A, class C>
concept triangular_left_shapes = requires(A a, C c) {
    std::linalg::triangular_matrix_left_product(
        a, std::linalg::upper_triangle,
        std::linalg::explicit_diagonal, c);
};

template<class A, class C>
concept triangular_right_shapes = requires(A a, C c) {
    std::linalg::triangular_matrix_right_product(
        a, std::linalg::upper_triangle,
        std::linalg::explicit_diagonal, c);
};

template<class A, class B, class C>
concept symmetric_matrix_shapes = requires(A a, B b, C c) {
    std::linalg::symmetric_matrix_product(
        a, std::linalg::upper_triangle, b, c);
    std::linalg::hermitian_matrix_product(
        a, std::linalg::upper_triangle, b, c);
};

template<class A, class C>
concept rank_k_shapes = requires(A a, C c) {
    std::linalg::symmetric_matrix_rank_k_update(
        1.0, a, c, std::linalg::upper_triangle);
    std::linalg::hermitian_matrix_rank_k_update(
        1.0, a, c, std::linalg::upper_triangle);
};

template<class A, class B, class C>
concept rank_2k_shapes = requires(A a, B b, C c) {
    std::linalg::symmetric_matrix_rank_2k_update(
        a, b, c, std::linalg::upper_triangle);
};

template<std::size_t Rows, std::size_t Columns>
using matrix = std::mdspan<double, std::extents<int, Rows, Columns>>;

template<std::size_t Size>
using vector = std::mdspan<double, std::extents<int, Size>>;

using dynamic_matrix = std::mdspan<double, std::dextents<int, 2>>;
using dynamic_vector = std::mdspan<double, std::dextents<int, 1>>;

static_assert(matrix_vector_shapes<matrix<2, 3>, vector<3>, vector<2>>);
static_assert(matrix_vector_shapes<matrix<2, 3>, dynamic_vector, vector<2>>);
static_assert(matrix_vector_update_shapes<
              matrix<2, 3>, vector<3>, vector<2>, vector<2>>);
static_assert(triangular_vector_shapes<matrix<2, 2>, vector<2>>);
static_assert(symmetric_vector_shapes<matrix<2, 2>, vector<2>, vector<2>>);
static_assert(rank_one_shapes<vector<2>, vector<3>, matrix<2, 3>>);
static_assert(symmetric_rank_two_shapes<vector<2>, vector<2>, matrix<2, 2>>);

static_assert(!matrix_vector_shapes<matrix<2, 4>, vector<3>, vector<2>>);
static_assert(!matrix_vector_shapes<matrix<2, 3>, vector<3>, vector<3>>);
static_assert(!matrix_vector_update_shapes<
              matrix<2, 3>, vector<3>, vector<2>, vector<3>>);
static_assert(!triangular_vector_shapes<matrix<2, 3>, vector<2>>);
static_assert(!triangular_vector_shapes<matrix<2, 2>, vector<3>>);
static_assert(!symmetric_vector_shapes<matrix<2, 2>, vector<2>, vector<3>>);
static_assert(!rank_one_shapes<vector<3>, vector<3>, matrix<2, 3>>);
static_assert(!symmetric_rank_two_shapes<vector<2>, vector<3>, matrix<2, 2>>);

static_assert(matrix_product_shapes<matrix<2, 3>, matrix<3, 4>, matrix<2, 4>>);
static_assert(matrix_product_shapes<dynamic_matrix, matrix<3, 4>, dynamic_matrix>);
static_assert(triangular_left_shapes<matrix<2, 2>, matrix<2, 3>>);
static_assert(triangular_right_shapes<matrix<2, 2>, matrix<3, 2>>);
static_assert(symmetric_matrix_shapes<matrix<2, 2>, matrix<2, 3>, matrix<2, 3>>);
static_assert(rank_k_shapes<matrix<3, 2>, matrix<3, 3>>);
static_assert(rank_2k_shapes<matrix<3, 2>, matrix<3, 2>, matrix<3, 3>>);

static_assert(!matrix_product_shapes<matrix<2, 4>, matrix<3, 2>, matrix<2, 2>>);
static_assert(!matrix_product_shapes<matrix<2, 3>, matrix<3, 4>, matrix<2, 3>>);
static_assert(!triangular_left_shapes<matrix<2, 2>, matrix<3, 2>>);
static_assert(!triangular_right_shapes<matrix<2, 2>, matrix<2, 3>>);
static_assert(!symmetric_matrix_shapes<matrix<2, 2>, matrix<2, 3>, matrix<2, 4>>);
static_assert(!rank_k_shapes<matrix<3, 2>, matrix<2, 2>>);
static_assert(!rank_2k_shapes<matrix<3, 2>, matrix<3, 3>, matrix<3, 3>>);

int main() {}
