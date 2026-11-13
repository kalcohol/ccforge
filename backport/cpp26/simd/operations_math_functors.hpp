#pragma once

namespace detail {

// Keep scalar callables outside vector function templates: MSVC can emit
// duplicate COMDATs for local generic lambdas shared by constexpr/runtime uses.
#define FORGE_SIMD_DETAIL_MATH_FUNCTION(name, scalar_name) \
struct math_##name##_fn { \
    template<class... T> \
    constexpr auto operator()(T... args) const { return scalar_name(args...); } \
};

FORGE_SIMD_DETAIL_MATH_FUNCTION(nearbyint, std::nearbyint)
FORGE_SIMD_DETAIL_MATH_FUNCTION(rint, std::rint)
FORGE_SIMD_DETAIL_MATH_FUNCTION(ilogb, std::ilogb)
FORGE_SIMD_DETAIL_MATH_FUNCTION(lrint, std::lrint)
FORGE_SIMD_DETAIL_MATH_FUNCTION(llrint, std::llrint)
FORGE_SIMD_DETAIL_MATH_FUNCTION(fmod, std::fmod)
FORGE_SIMD_DETAIL_MATH_FUNCTION(remainder, std::remainder)
FORGE_SIMD_DETAIL_MATH_FUNCTION(nextafter, std::nextafter)
FORGE_SIMD_DETAIL_MATH_FUNCTION(fdim, std::fdim)
FORGE_SIMD_DETAIL_MATH_FUNCTION(fma, std::fma)
FORGE_SIMD_DETAIL_MATH_FUNCTION(isfinite, std::isfinite)
FORGE_SIMD_DETAIL_MATH_FUNCTION(isinf, std::isinf)
FORGE_SIMD_DETAIL_MATH_FUNCTION(isnan, std::isnan)
FORGE_SIMD_DETAIL_MATH_FUNCTION(isnormal, std::isnormal)
FORGE_SIMD_DETAIL_MATH_FUNCTION(isgreater, std::isgreater)
FORGE_SIMD_DETAIL_MATH_FUNCTION(isgreaterequal, std::isgreaterequal)
FORGE_SIMD_DETAIL_MATH_FUNCTION(isless, std::isless)
FORGE_SIMD_DETAIL_MATH_FUNCTION(islessequal, std::islessequal)
FORGE_SIMD_DETAIL_MATH_FUNCTION(islessgreater, std::islessgreater)
FORGE_SIMD_DETAIL_MATH_FUNCTION(isunordered, std::isunordered)
FORGE_SIMD_DETAIL_MATH_FUNCTION(sqrt, std::sqrt)
FORGE_SIMD_DETAIL_MATH_FUNCTION(sin, std::sin)
FORGE_SIMD_DETAIL_MATH_FUNCTION(cos, std::cos)
FORGE_SIMD_DETAIL_MATH_FUNCTION(tan, std::tan)
FORGE_SIMD_DETAIL_MATH_FUNCTION(asin, std::asin)
FORGE_SIMD_DETAIL_MATH_FUNCTION(acos, std::acos)
FORGE_SIMD_DETAIL_MATH_FUNCTION(atan, std::atan)
FORGE_SIMD_DETAIL_MATH_FUNCTION(sinh, std::sinh)
FORGE_SIMD_DETAIL_MATH_FUNCTION(cosh, std::cosh)
FORGE_SIMD_DETAIL_MATH_FUNCTION(tanh, std::tanh)
FORGE_SIMD_DETAIL_MATH_FUNCTION(asinh, std::asinh)
FORGE_SIMD_DETAIL_MATH_FUNCTION(acosh, std::acosh)
FORGE_SIMD_DETAIL_MATH_FUNCTION(atanh, std::atanh)
FORGE_SIMD_DETAIL_MATH_FUNCTION(exp, std::exp)
FORGE_SIMD_DETAIL_MATH_FUNCTION(exp2, std::exp2)
FORGE_SIMD_DETAIL_MATH_FUNCTION(expm1, std::expm1)
FORGE_SIMD_DETAIL_MATH_FUNCTION(log, std::log)
FORGE_SIMD_DETAIL_MATH_FUNCTION(log10, std::log10)
FORGE_SIMD_DETAIL_MATH_FUNCTION(log1p, std::log1p)
FORGE_SIMD_DETAIL_MATH_FUNCTION(log2, std::log2)
FORGE_SIMD_DETAIL_MATH_FUNCTION(logb, std::logb)
FORGE_SIMD_DETAIL_MATH_FUNCTION(cbrt, std::cbrt)
FORGE_SIMD_DETAIL_MATH_FUNCTION(erf, std::erf)
FORGE_SIMD_DETAIL_MATH_FUNCTION(erfc, std::erfc)
FORGE_SIMD_DETAIL_MATH_FUNCTION(lgamma, std::lgamma)
FORGE_SIMD_DETAIL_MATH_FUNCTION(tgamma, std::tgamma)
FORGE_SIMD_DETAIL_MATH_FUNCTION(atan2, std::atan2)
FORGE_SIMD_DETAIL_MATH_FUNCTION(pow, std::pow)
FORGE_SIMD_DETAIL_MATH_FUNCTION(hypot, std::hypot)
FORGE_SIMD_DETAIL_MATH_FUNCTION(lerp, std::lerp)

FORGE_SIMD_DETAIL_MATH_FUNCTION(fabs, detail::math_fabs)
FORGE_SIMD_DETAIL_MATH_FUNCTION(ceil, detail::math_ceil)
FORGE_SIMD_DETAIL_MATH_FUNCTION(floor, detail::math_floor)
FORGE_SIMD_DETAIL_MATH_FUNCTION(round, detail::math_round)
FORGE_SIMD_DETAIL_MATH_FUNCTION(trunc, detail::math_trunc)
FORGE_SIMD_DETAIL_MATH_FUNCTION(copysign, detail::math_copysign)
FORGE_SIMD_DETAIL_MATH_FUNCTION(fmax, detail::math_fmax)
FORGE_SIMD_DETAIL_MATH_FUNCTION(fmin, detail::math_fmin)
FORGE_SIMD_DETAIL_MATH_FUNCTION(fpclassify, detail::math_fpclassify)
FORGE_SIMD_DETAIL_MATH_FUNCTION(signbit, detail::math_signbit)

#undef FORGE_SIMD_DETAIL_MATH_FUNCTION

struct math_signed_abs_fn {
    template<class T>
    constexpr auto operator()(T lane) const { return lane < 0 ? -lane : lane; }
};

template<class Integer>
struct math_round_to_integer_fn {
    template<class T>
    constexpr auto operator()(T lane) const {
        return detail::math_round_to_integer<Integer>(lane);
    }
};

} // namespace detail
