// Compile probe: BLAS-1 algorithms reject statically incompatible operands
// while retaining dynamic/static compatible forms.

#include <linalg>
#include <mdspan>

template<class X, class Y>
concept copyable_shapes = requires(X x, Y y) {
    std::linalg::copy(x, y);
};

template<class X, class Y>
concept swappable_shapes = requires(X x, Y y) {
    std::linalg::swap_elements(x, y);
};

template<class X, class Y, class Z>
concept addable_shapes = requires(X x, Y y, Z z) {
    std::linalg::add(x, y, z);
};

template<class X, class Y>
concept dot_shapes = requires(X x, Y y) {
    std::linalg::dot(x, y, 0.0);
    std::linalg::dotc(x, y, 0.0);
};

template<class X, class Y>
concept givens_shapes = requires(X x, Y y) {
    std::linalg::apply_givens_rotation(x, y, 0.6, 0.8);
};

using static3 = std::mdspan<double, std::extents<int, 3>>;
using static4 = std::mdspan<double, std::extents<int, 4>>;
using dynamic1 = std::mdspan<double, std::dextents<int, 1>>;

static_assert(copyable_shapes<static3, static3>);
static_assert(swappable_shapes<static3, dynamic1>);
static_assert(addable_shapes<static3, dynamic1, static3>);
static_assert(dot_shapes<dynamic1, static3>);
static_assert(givens_shapes<static3, dynamic1>);

static_assert(!copyable_shapes<static3, static4>);
static_assert(!swappable_shapes<static3, static4>);
static_assert(!addable_shapes<static3, static4, static3>);
static_assert(!addable_shapes<static3, static3, static4>);
static_assert(!dot_shapes<static3, static4>);
static_assert(!givens_shapes<static3, static4>);

int main() {}
