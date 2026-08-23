#include <linalg>

#include <concepts>
#include <mdspan>
#include <type_traits>

namespace {

using extents_2x2 = std::extents<std::size_t, 2, 2>;
using extents_2 = std::extents<std::size_t, 2>;
using upper_layout = std::linalg::layout_blas_packed<
    std::linalg::upper_triangle_t,
    std::linalg::row_major_t>;
using lower_layout = std::linalg::layout_blas_packed<
    std::linalg::lower_triangle_t,
    std::linalg::row_major_t>;
using upper_matrix = std::mdspan<double, extents_2x2, upper_layout>;
using lower_matrix = std::mdspan<double, extents_2x2, lower_layout>;
using dense_matrix = std::mdspan<double, extents_2x2>;
using vector = std::mdspan<double, extents_2>;

static_assert(std::same_as<upper_layout::triangle_type,
                           std::linalg::upper_triangle_t>);
static_assert(std::same_as<upper_layout::storage_order_type,
                           std::linalg::row_major_t>);

template<class Matrix>
concept accepts_upper_triangular_input = requires(Matrix matrix, vector values) {
    std::linalg::triangular_matrix_vector_product(
        matrix,
        std::linalg::upper_triangle,
        std::linalg::explicit_diagonal,
        values);
};

template<class Matrix>
concept accepts_upper_rank1_output = requires(Matrix matrix, vector values) {
    std::linalg::symmetric_matrix_rank_1_update(
        1.0, values, matrix, std::linalg::upper_triangle);
};

template<class Input, class Output>
concept accepts_upper_rankk_update = requires(
    dense_matrix factors, Input input, Output output) {
    std::linalg::symmetric_matrix_rank_k_update(
        1.0, factors, input, output, std::linalg::upper_triangle);
};

static_assert(accepts_upper_triangular_input<upper_matrix>);
static_assert(!accepts_upper_triangular_input<lower_matrix>);
static_assert(accepts_upper_triangular_input<dense_matrix>);

static_assert(accepts_upper_rank1_output<upper_matrix>);
static_assert(!accepts_upper_rank1_output<lower_matrix>);

static_assert(accepts_upper_rankk_update<upper_matrix, upper_matrix>);
static_assert(!accepts_upper_rankk_update<lower_matrix, upper_matrix>);
static_assert(!accepts_upper_rankk_update<upper_matrix, lower_matrix>);

} // namespace

int main() {}
