#pragma once

#include <linalg>

#include <array>
#include <complex>
#include <concepts>
#include <cstddef>
#include <mdspan>
#include <type_traits>
#include <utility>

namespace unique_mapping_probe {

namespace la = std::linalg;

// [mdspan.layout.reqmts] permits a conservative false is_always_unique().
struct conservative_layout {
    template<class Extents>
    class mapping {
        using nested_mapping = std::layout_right::mapping<Extents>;

    public:
        using extents_type = Extents;
        using index_type = typename Extents::index_type;
        using size_type = typename Extents::size_type;
        using rank_type = typename Extents::rank_type;
        using layout_type = conservative_layout;

        constexpr mapping() noexcept = default;
        constexpr mapping(const Extents& extents) noexcept : nested_(extents) {}

        constexpr const Extents& extents() const noexcept {
            return nested_.extents();
        }

        constexpr index_type required_span_size() const noexcept {
            return nested_.required_span_size();
        }

        template<class... Indices>
            requires (sizeof...(Indices) == Extents::rank())
        constexpr index_type operator()(Indices... indices) const noexcept {
            return nested_(indices...);
        }

        static constexpr bool is_always_unique() noexcept { return false; }
        static constexpr bool is_always_exhaustive() noexcept {
            return nested_mapping::is_always_exhaustive();
        }
        static constexpr bool is_always_strided() noexcept {
            return nested_mapping::is_always_strided();
        }
        constexpr bool is_unique() const noexcept { return nested_.is_unique(); }
        constexpr bool is_exhaustive() const noexcept { return nested_.is_exhaustive(); }
        constexpr bool is_strided() const noexcept { return nested_.is_strided(); }
        constexpr index_type stride(rank_type rank) const noexcept {
            return nested_.stride(rank);
        }

        friend constexpr bool operator==(const mapping&, const mapping&) noexcept = default;

    private:
        nested_mapping nested_{};
    };
};

struct write_proxy {
    double* value;

    explicit write_proxy(double* input) : value(input) {}
    write_proxy(const write_proxy&) = delete;
    write_proxy(write_proxy&&) = delete;

    operator double() const { return *value; }

    write_proxy&& operator=(double input) && {
        *value = input;
        return std::move(*this);
    }
};

struct proxy_accessor {
    using offset_policy = proxy_accessor;
    using element_type = double;
    using reference = write_proxy;
    using data_handle_type = double*;

    int* accesses{};

    reference access(data_handle_type pointer, std::size_t index) const {
        if (accesses) ++*accesses;
        return write_proxy{pointer + index};
    }

    data_handle_type offset(data_handle_type pointer, std::size_t index) const {
        return pointer + index;
    }
};

using extents_2 = std::extents<int, 2>;
using extents_2x2 = std::extents<int, 2, 2>;
using vector = std::mdspan<double, extents_2>;
using matrix = std::mdspan<double, extents_2x2>;
using weak_vector = std::mdspan<double, extents_2, conservative_layout>;
using weak_matrix = std::mdspan<double, extents_2x2, conservative_layout>;
using complex_vector = std::mdspan<std::complex<double>, extents_2>;
using weak_complex_vector = std::mdspan<std::complex<double>, extents_2, conservative_layout>;
using proxy_vector = std::mdspan<double, extents_2, std::layout_right, proxy_accessor>;
using proxy_matrix = std::mdspan<double, extents_2x2, std::layout_right, proxy_accessor>;
using upper_layout = la::layout_blas_packed<la::upper_triangle_t, la::row_major_t>;
using lower_layout = la::layout_blas_packed<la::lower_triangle_t, la::column_major_t>;
using upper_matrix = std::mdspan<double, extents_2x2, upper_layout>;
using lower_matrix = std::mdspan<double, extents_2x2, lower_layout>;

template<class Out>
using dense_for = std::mdspan<typename Out::value_type, typename Out::extents_type>;

template<class Out>
using weak_for = std::mdspan<typename Out::value_type, typename Out::extents_type,
                            conservative_layout>;

template<class Out>
using vector_for = std::mdspan<typename Out::value_type,
    std::extents<int, Out::static_extent(0)>>;

template<class Out>
using matrix_for = std::mdspan<typename Out::value_type,
    std::extents<int, Out::static_extent(0), Out::static_extent(0)>>;

template<std::size_t N>
consteval bool all_equal(const std::array<bool, N>& calls, bool expected) {
    for (bool call : calls) {
        if (call != expected) return false;
    }
    return true;
}

template<class Out>
constexpr auto matrix_writes = std::array{
    requires(dense_for<Out> a, Out c) { la::copy(a, c); },
    requires(Out c) { la::scale(2.0, c); },
    requires(dense_for<Out> a, Out c) { la::swap_elements(a, c); },
    requires(dense_for<Out> a, Out c) { la::swap_elements(c, a); },
    requires(dense_for<Out> a, Out c) { la::add(a, a, c); },
    requires(vector_for<Out> x, Out c) { la::matrix_rank_1_update(x, x, c); },
    requires(vector_for<Out> x, weak_for<Out> e, Out c) {
        la::matrix_rank_1_update(x, x, e, c);
    },
    requires(vector_for<Out> x, Out c) { la::matrix_rank_1_update_c(x, x, c); },
    requires(vector_for<Out> x, weak_for<Out> e, Out c) {
        la::matrix_rank_1_update_c(x, x, e, c);
    },
    requires(dense_for<Out> a, Out c) { la::matrix_product(a, a, c); },
    requires(dense_for<Out> a, weak_for<Out> e, Out c) {
        la::matrix_product(a, a, e, c);
    },
    requires(dense_for<Out> a, Out c) {
        la::triangular_matrix_left_product(a, la::upper_triangle, la::explicit_diagonal, c);
    },
    requires(dense_for<Out> a, Out c) {
        la::triangular_matrix_right_product(a, la::upper_triangle, la::explicit_diagonal, c);
    },
    requires(dense_for<Out> a, Out c) {
        la::triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, c);
    },
    requires(dense_for<Out> a, Out c) {
        la::symmetric_matrix_product(a, la::upper_triangle, a, c);
    },
    requires(dense_for<Out> a, Out c) {
        la::hermitian_matrix_product(a, la::upper_triangle, a, c);
    },
    requires(dense_for<Out> a, Out c) {
        la::hermitian_matrix_product(la::upper_triangle, a, a, c);
    },
};

template<class Out>
constexpr auto vector_writes = std::array{
    requires(dense_for<Out> x, Out y) { la::copy(x, y); },
    requires(Out y) { la::scale(2.0, y); },
    requires(dense_for<Out> x, Out y) { la::swap_elements(x, y); },
    requires(dense_for<Out> x, Out y) { la::swap_elements(y, x); },
    requires(dense_for<Out> x, Out y) { la::add(x, x, y); },
    requires(dense_for<Out> x, Out y) { la::apply_givens_rotation(x, y, 1.0, 0.0); },
    requires(dense_for<Out> x, Out y) { la::apply_givens_rotation(y, x, 1.0, 0.0); },
    requires(matrix_for<Out> a, dense_for<Out> x, Out y) {
        la::matrix_vector_product(a, x, y);
    },
    requires(matrix_for<Out> a, dense_for<Out> x, weak_for<Out> e, Out y) {
        la::matrix_vector_product(a, x, e, y);
    },
    requires(matrix_for<Out> a, Out y) {
        la::triangular_matrix_vector_product(a, la::upper_triangle, la::explicit_diagonal, y);
    },
    requires(matrix_for<Out> a, dense_for<Out> x, Out y) {
        la::triangular_matrix_vector_product(a, la::upper_triangle, la::explicit_diagonal, x, y);
    },
    requires(matrix_for<Out> a, Out y) {
        la::triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, y);
    },
    requires(matrix_for<Out> a, dense_for<Out> x, Out y) {
        la::symmetric_matrix_vector_product(a, la::upper_triangle, x, y);
    },
    requires(matrix_for<Out> a, dense_for<Out> x, weak_for<Out> e, Out y) {
        la::symmetric_matrix_vector_product(a, la::upper_triangle, x, e, y);
    },
    requires(matrix_for<Out> a, dense_for<Out> x, Out y) {
        la::hermitian_matrix_vector_product(a, la::upper_triangle, x, y);
    },
    requires(matrix_for<Out> a, dense_for<Out> x, weak_for<Out> e, Out y) {
        la::hermitian_matrix_vector_product(a, la::upper_triangle, x, e, y);
    },
};

template<class Out>
constexpr auto complex_rotations = std::array{
    requires(dense_for<Out> x, Out y) {
        la::apply_givens_rotation(x, y, 1.0, std::complex<double>{});
    },
    requires(dense_for<Out> x, Out y) {
        la::apply_givens_rotation(y, x, 1.0, std::complex<double>{});
    },
};

template<class Out, class Triangle = la::upper_triangle_t>
constexpr auto structured_writes = std::array{
    requires(vector_for<Out> x, Out c, Triangle t) {
        la::symmetric_matrix_rank_1_update(1.0, x, c, t);
    },
    requires(vector_for<Out> x, weak_for<Out> e, Out c, Triangle t) {
        la::symmetric_matrix_rank_1_update(1.0, x, e, c, t);
    },
    requires(vector_for<Out> x, Out c, Triangle t) {
        la::symmetric_matrix_rank_1_update(1.0, t, x, c);
    },
    requires(vector_for<Out> x, Out c, Triangle t) {
        la::hermitian_matrix_rank_1_update(1.0, x, c, t);
    },
    requires(vector_for<Out> x, weak_for<Out> e, Out c, Triangle t) {
        la::hermitian_matrix_rank_1_update(1.0, x, e, c, t);
    },
    requires(vector_for<Out> x, Out c, Triangle t) {
        la::hermitian_matrix_rank_1_update(t, x, c);
    },
    requires(vector_for<Out> x, Out c, Triangle t) {
        la::symmetric_matrix_rank_2_update(x, x, c, t);
    },
    requires(vector_for<Out> x, weak_for<Out> e, Out c, Triangle t) {
        la::symmetric_matrix_rank_2_update(x, x, e, c, t);
    },
    requires(vector_for<Out> x, Out c, Triangle t) {
        la::symmetric_matrix_rank_2_update(1.0, t, x, x, c);
    },
    requires(vector_for<Out> x, Out c, Triangle t) {
        la::hermitian_matrix_rank_2_update(x, x, c, t);
    },
    requires(vector_for<Out> x, weak_for<Out> e, Out c, Triangle t) {
        la::hermitian_matrix_rank_2_update(x, x, e, c, t);
    },
    requires(vector_for<Out> x, Out c, Triangle t) {
        la::hermitian_matrix_rank_2_update(t, x, x, c);
    },
    requires(dense_for<Out> a, Out c, Triangle t) {
        la::symmetric_matrix_rank_k_update(1.0, a, c, t);
    },
    requires(dense_for<Out> a, weak_for<Out> e, Out c, Triangle t) {
        la::symmetric_matrix_rank_k_update(1.0, a, e, c, t);
    },
    requires(dense_for<Out> a, Out c, Triangle t) {
        la::symmetric_matrix_rank_k_update(1.0, t, a, c);
    },
    requires(dense_for<Out> a, Out c, Triangle t) {
        la::hermitian_matrix_rank_k_update(1.0, a, c, t);
    },
    requires(dense_for<Out> a, weak_for<Out> e, Out c, Triangle t) {
        la::hermitian_matrix_rank_k_update(1.0, a, e, c, t);
    },
    requires(dense_for<Out> a, Out c, Triangle t) {
        la::hermitian_matrix_rank_k_update(t, a, c);
    },
    requires(dense_for<Out> a, Out c, Triangle t) {
        la::symmetric_matrix_rank_2k_update(a, a, c, t);
    },
    requires(dense_for<Out> a, weak_for<Out> e, Out c, Triangle t) {
        la::symmetric_matrix_rank_2k_update(a, a, e, c, t);
    },
    requires(dense_for<Out> a, Out c, Triangle t) {
        la::symmetric_matrix_rank_2k_update(1.0, t, a, a, c);
    },
};

template<class In, class Triangle = la::upper_triangle_t>
constexpr auto matrix_reads = std::array{
    requires(In a, dense_for<In> c) { la::copy(a, c); },
    requires(In a, vector_for<In> x, vector_for<In> y) {
        la::matrix_vector_product(a, x, y);
    },
    requires(In a, vector_for<In> x, vector_for<In> y, Triangle t) {
        la::symmetric_matrix_vector_product(a, t, x, y);
    },
    requires(In a, vector_for<In> x, vector_for<In> y, Triangle t) {
        la::triangular_matrix_vector_product(a, t, la::explicit_diagonal, x, y);
    },
    requires(In a, dense_for<In> b, dense_for<In> c) { la::matrix_product(a, b, c); },
    requires(In a, dense_for<In> b, dense_for<In> c, Triangle t) {
        la::symmetric_matrix_product(a, t, b, c);
    },
    requires(In a) { la::matrix_frob_norm(a, 0.0); },
    requires(In a) { la::matrix_one_norm(a, 0.0); },
    requires(In a) { la::matrix_inf_norm(a, 0.0); },
};

template<class In>
constexpr auto vector_reads = std::array{
    requires(In x, dense_for<In> y) { la::copy(x, y); },
    requires(In x, dense_for<In> y) { la::dot(x, y, 0.0); },
    requires(In x, dense_for<In> y) { la::dotc(x, y, 0.0); },
    requires(In x) { la::vector_two_norm(x, 0.0); },
    requires(In x) { la::vector_abs_sum(x, 0.0); },
    requires(In x) { la::vector_idx_abs_max(x); },
};

} // namespace unique_mapping_probe
