// Compile probe: BLAS-1 algorithms reject statically incompatible operands
// while retaining dynamic/static compatible forms.

#include <linalg>
#include <mdspan>
#include <array>

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
consteval bool dot_overloads_match(bool expected) {
    const std::array calls{
        requires(X x, Y y) { std::linalg::dot(x, y); },
        requires(X x, Y y) { std::linalg::dot(x, y, 0.0); },
        requires(X x, Y y) { std::linalg::dotc(x, y); },
        requires(X x, Y y) { std::linalg::dotc(x, y, 0.0); },
    };
    for (bool participates : calls) {
        if (participates != expected) return false;
    }
    return true;
}

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

using rank0 = std::mdspan<double, std::extents<int>>;
using rank2 = std::mdspan<double, std::extents<int, 3, 1>>;
using dynamic2 = std::mdspan<double, std::dextents<int, 2>>;

static_assert(dot_overloads_match<static3, static3>(true));
static_assert(dot_overloads_match<static3, dynamic1>(true));
static_assert(dot_overloads_match<dynamic1, static3>(true));
static_assert(dot_overloads_match<dynamic1, dynamic1>(true));

static_assert(dot_overloads_match<rank0, static3>(false));
static_assert(dot_overloads_match<static3, rank0>(false));
static_assert(dot_overloads_match<rank0, dynamic1>(false));
static_assert(dot_overloads_match<dynamic1, rank0>(false));
static_assert(dot_overloads_match<rank0, rank0>(false));

static_assert(dot_overloads_match<rank2, static3>(false));
static_assert(dot_overloads_match<static3, rank2>(false));
static_assert(dot_overloads_match<rank2, dynamic1>(false));
static_assert(dot_overloads_match<dynamic1, rank2>(false));
static_assert(dot_overloads_match<rank2, rank2>(false));
static_assert(dot_overloads_match<dynamic2, dynamic2>(false));
static_assert(dot_overloads_match<rank2, dynamic2>(false));
static_assert(dot_overloads_match<dynamic2, rank2>(false));

int main() {}
