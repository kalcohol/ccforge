#include "../unique_mapping_probe.hpp"

namespace p = unique_mapping_probe;

static_assert(std::semiregular<p::conservative_layout::mapping<p::extents_2>>);
static_assert(std::equality_comparable<p::conservative_layout::mapping<p::extents_2>>);
static_assert(std::is_nothrow_move_constructible_v<
    p::conservative_layout::mapping<p::extents_2>>);
static_assert(std::is_nothrow_move_assignable_v<
    p::conservative_layout::mapping<p::extents_2>>);
static_assert(std::is_nothrow_swappable_v<p::conservative_layout::mapping<p::extents_2>>);
static_assert(p::conservative_layout::mapping<p::extents_2>{}.is_unique());
static_assert(!p::weak_vector::is_always_unique());
static_assert(!p::upper_matrix::is_always_unique());
static_assert(std::semiregular<p::proxy_accessor>);
static_assert(std::common_reference_with<p::write_proxy&&, double&>);
static_assert(std::is_assignable_v<p::write_proxy, double>);
static_assert(!std::is_copy_constructible_v<p::write_proxy>);

static_assert(p::all_equal(p::matrix_writes<p::matrix>, true));
static_assert(p::all_equal(p::vector_writes<p::vector>, true));
static_assert(p::all_equal(p::matrix_writes<p::proxy_matrix>, true));
static_assert(p::all_equal(p::vector_writes<p::proxy_vector>, true));
static_assert(p::all_equal(p::complex_rotations<p::complex_vector>, true));
static_assert(p::all_equal(p::structured_writes<p::proxy_matrix>, true));
static_assert(p::all_equal(p::matrix_writes<p::weak_matrix>, false));
static_assert(p::all_equal(p::vector_writes<p::weak_vector>, false));
static_assert(p::all_equal(p::complex_rotations<p::weak_complex_vector>, false));
static_assert(p::all_equal(p::matrix_writes<p::upper_matrix>, false));
static_assert(p::all_equal(p::matrix_writes<p::lower_matrix>, false));
static_assert(p::all_equal(p::structured_writes<p::weak_matrix>, false));
static_assert(p::all_equal(p::structured_writes<p::upper_matrix>, true));
static_assert(p::all_equal(p::structured_writes<p::lower_matrix,
    p::la::lower_triangle_t>, true));

using left_matrix = std::mdspan<double, p::extents_2x2, std::layout_left>;
using stride_matrix = std::mdspan<double, p::extents_2x2, std::layout_stride>;
using stride_vector = std::mdspan<double, p::extents_2, std::layout_stride>;
using transposed_input = std::mdspan<double, std::extents<std::size_t, 2, 2>>;
using transposed_matrix = decltype(p::la::transposed(std::declval<transposed_input>()));
using const_packed = std::mdspan<const double, p::extents_2x2, p::upper_layout>;
using empty_packed = std::mdspan<double, std::extents<int, 0, 0>, p::upper_layout>;
using scalar_packed = std::mdspan<double, std::extents<int, 1, 1>, p::upper_layout>;
using dynamic_packed = std::mdspan<double, std::dextents<int, 2>, p::upper_layout>;

static_assert(p::all_equal(p::matrix_writes<left_matrix>, true));
static_assert(p::all_equal(p::matrix_writes<stride_matrix>, true));
static_assert(p::all_equal(p::vector_writes<stride_vector>, true));
static_assert(p::all_equal(p::matrix_writes<transposed_matrix>, true));
static_assert(p::all_equal(p::matrix_reads<p::weak_matrix>, true));
static_assert(p::all_equal(p::matrix_reads<const_packed>, true));
static_assert(p::all_equal(p::vector_reads<p::weak_vector>, true));
static_assert(p::all_equal(p::matrix_writes<empty_packed>, true));
static_assert(p::all_equal(p::matrix_writes<scalar_packed>, true));
static_assert(p::all_equal(p::matrix_writes<dynamic_packed>, false));
static_assert(p::all_equal(p::structured_writes<dynamic_packed>, true));

int main() {}
