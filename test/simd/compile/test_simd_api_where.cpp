#include "simd_test_common.hpp"

namespace {

using namespace simd_test;

template<class Expr, class Shift, class = void>
struct has_where_lshift_assign : std::false_type {};

template<class Expr, class Shift>
struct has_where_lshift_assign<Expr,
                               Shift,
                               std::void_t<decltype(std::declval<Expr&>() <<= std::declval<const Shift&>())>>
    : std::true_type {};

template<class Expr, class Shift, class = void>
struct has_where_rshift_assign : std::false_type {};

template<class Expr, class Shift>
struct has_where_rshift_assign<Expr,
                               Shift,
                               std::void_t<decltype(std::declval<Expr&>() >>= std::declval<const Shift&>())>>
    : std::true_type {};

static_assert(std::is_same<decltype(std::simd::select(mask4{}, mask4{}, mask4{})), mask4>::value,
    "select(mask, mask, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::select(mask4{}, 1, 2)), int4>::value,
    "select(mask, scalar, scalar) should produce the lane vector type");
static_assert(std::is_same<decltype(std::simd::simd_cast<longlong4>(std::declval<const int4&>())), longlong4>::value,
    "simd_cast should be a public entry point for value-preserving conversions");
static_assert(std::is_same<decltype(std::simd::static_simd_cast<int4>(std::declval<const longlong4&>())), int4>::value,
    "static_simd_cast should be a public entry point for narrowing conversions");
static_assert(requires(int4 value, mask4 mask_value) { std::simd::where(mask_value, value) = value; },
    "where(mask, vec) should allow masked assignment from vectors");
static_assert(requires(int4 value, mask4 mask_value) { std::simd::where(mask_value, value) = 1; },
    "where(mask, vec) should allow masked assignment from scalars");
static_assert(requires(int4 value, mask4 mask_value) { std::simd::where(mask_value, value).copy_from(static_cast<const int*>(nullptr)); },
    "where(mask, vec) should expose copy_from for pointers");
static_assert(requires(const int4 value, mask4 mask_value) { std::simd::where(mask_value, value).copy_to(static_cast<int*>(nullptr)); },
    "where(mask, vec) should expose copy_to for const vectors");
static_assert(requires(int4 value, mask4 mask_value) { std::simd::where(mask_value, value) += value; },
    "where(mask, vec) should support compound assignment with vectors");
static_assert(requires(int4 value, mask4 mask_value) { std::simd::where(mask_value, value) *= 2; },
    "where(mask, vec) should support compound assignment with scalars");

using where_vec_expr = decltype(std::simd::where(std::declval<mask4>(), std::declval<int4&>()));
using where_float_expr = decltype(std::simd::where(std::declval<typename float4::mask_type>(), std::declval<float4&>()));

static_assert(std::is_same<decltype((std::declval<where_vec_expr&>() /= std::declval<const int4&>())), where_vec_expr&>::value,
    "where_expression should return itself from operator/=(vec)");
static_assert(std::is_same<decltype((std::declval<where_vec_expr&>() <<= 1)), where_vec_expr&>::value,
    "where_expression should return itself from operator<<=(shift)");
static_assert(std::is_same<decltype((std::declval<where_vec_expr&>() <<= std::declval<const int4&>())), where_vec_expr&>::value,
    "where_expression should return itself from operator<<=(vec)");
static_assert(std::is_same<decltype((std::declval<where_vec_expr&>() >>= std::declval<const int4&>())), where_vec_expr&>::value,
    "where_expression should return itself from operator>>=(vec)");
static_assert(requires(where_vec_expr& expr, const int4& rhs) { expr ^= rhs; },
    "where_expression should support integer operator^=(vec) for integral vectors");
static_assert(std::is_same<decltype((std::declval<where_vec_expr&>() &= 1)), where_vec_expr&>::value,
    "where_expression should return itself from operator&=(scalar)");
static_assert(requires(where_vec_expr& expr) { expr %= 3; },
    "where_expression should support integer operator%=(scalar) for integral vectors");
static_assert(!has_where_lshift_assign<where_float_expr, float4>::value,
    "where_expression should not expose vector left shift for floating vectors");
static_assert(!has_where_rshift_assign<where_float_expr, float4>::value,
    "where_expression should not expose vector right shift for floating vectors");
static_assert(requires(int4 value) { std::simd::where(true, value) = value; },
    "where(bool, vec) should allow masked assignment from vectors");
static_assert(requires(int4 value) { std::simd::where(false, value) = 1; },
    "where(bool, vec) should allow masked assignment from scalars");
static_assert(requires(mask4 cond, mask4 value) { std::simd::where(cond, value) = value; },
    "where(mask, mask) should allow masked assignment between masks");
static_assert(std::is_same<decltype(std::simd::simd_cast<std::simd::mask<long long, 4>>(std::declval<const mask4&>())),
                  std::simd::mask<long long, 4>>::value,
    "simd_cast should be a public entry point for mask conversions");
static_assert(std::is_same<decltype(std::simd::static_simd_cast<mask4>(std::declval<const std::simd::mask<long long, 4>&>())), mask4>::value,
    "static_simd_cast should be a public entry point for mask conversions");
static_assert(std::is_same<decltype(std::simd::compress(std::declval<const int4&>(), mask4{}, 7)), int4>::value,
    "compress(vec, mask, fill) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::expand(std::declval<const int4&>(), mask4{}, std::declval<const int4&>())), int4>::value,
    "expand(vec, mask, original) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::compress(mask4{}, mask4{})), mask4>::value,
    "compress(mask, mask) should be a public entry point");
static_assert(std::is_same<decltype(std::simd::expand(mask4{}, mask4{}, mask4{})), mask4>::value,
    "expand(mask, mask, original) should be a public entry point");

} // namespace

int main() {
    return 0;
}
