// MIT License
//
// Copyright (c) 2026 CC Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

// Vector extension type aliases and dispatch helpers for basic_vec Layer 1.
// Uses GCC/Clang vector_size attribute with CONCRETE byte sizes.
// Only enabled when compiler supports the required ISA.

#include <cstddef>
#include <type_traits>

namespace detail {

// ─── Concrete vector type aliases ────────────────────────────────────────────
// vector_size takes TOTAL BYTES, not element count.

#if defined(__SSE2__) || defined(__ARM_NEON) || defined(__aarch64__)
typedef float  __attribute__((vector_size(16))) __vec_f32x4;
typedef double __attribute__((vector_size(16))) __vec_f64x2;
typedef int    __attribute__((vector_size(16))) __vec_i32x4;
typedef long   __attribute__((vector_size(16))) __vec_i64x2;
typedef unsigned int  __attribute__((vector_size(16))) __vec_u32x4;
typedef unsigned long __attribute__((vector_size(16))) __vec_u64x2;
#define __SIMD_HAVE_128BIT 1
#endif

#if defined(__AVX2__) || defined(__AVX__)
typedef float  __attribute__((vector_size(32))) __vec_f32x8;
typedef double __attribute__((vector_size(32))) __vec_f64x4;
typedef int    __attribute__((vector_size(32))) __vec_i32x8;
typedef long   __attribute__((vector_size(32))) __vec_i64x4;
typedef unsigned int  __attribute__((vector_size(32))) __vec_u32x8;
typedef unsigned long __attribute__((vector_size(32))) __vec_u64x4;
#define __SIMD_HAVE_256BIT 1
#endif

#if defined(__AVX512F__)
typedef float  __attribute__((vector_size(64))) __vec_f32x16;
typedef double __attribute__((vector_size(64))) __vec_f64x8;
typedef int    __attribute__((vector_size(64))) __vec_i32x16;
typedef long   __attribute__((vector_size(64))) __vec_i64x8;
typedef unsigned int  __attribute__((vector_size(64))) __vec_u32x16;
typedef unsigned long __attribute__((vector_size(64))) __vec_u64x8;
#define __SIMD_HAVE_512BIT 1
#endif

// ─── is_constant_evaluated helper ────────────────────────────────────────────
// __builtin_is_constant_evaluated() is GCC 9+/Clang 9+

#if defined(__has_builtin) && __has_builtin(__builtin_is_constant_evaluated)
#define __SIMD_IS_CONSTEVAL() __builtin_is_constant_evaluated()
#elif __cpp_lib_is_constant_evaluated >= 201811L
#include <utility>
#define __SIMD_IS_CONSTEVAL() ::std::is_constant_evaluated()
#else
#define __SIMD_IS_CONSTEVAL() false
#endif

// ─── Binary op dispatch ───────────────────────────────────────────────────────
// Macro to generate elementwise-op dispatch for a given (ElementType, N, VecType).
// Falls through to scalar loop for non-matching combinations.

#define __SIMD_VECOP2(T, N, VecType, op)                                    \
    if constexpr (::std::is_same_v<T, decltype(T())> &&                       \
                  sizeof(T[N]) == sizeof(VecType)) {                        \
        VecType& vd = reinterpret_cast<VecType&>(*dst);                     \
        const VecType& vs = reinterpret_cast<const VecType&>(*src);         \
        vd op vs;                                                            \
        return;                                                              \
    }

// ─── Vector add/sub/mul helpers ──────────────────────────────────────────────

template<class T, ::std::size_t N>
inline void __simd_add(T* __restrict__ dst, const T* __restrict__ src) noexcept {
    if (__SIMD_IS_CONSTEVAL()) { for (::std::size_t i=0;i<N;++i) dst[i]+=src[i]; return; }
#ifdef __SIMD_HAVE_512BIT
    if constexpr (::std::is_same_v<T,float>  && N==16) { reinterpret_cast<__vec_f32x16&>(*dst) += reinterpret_cast<const __vec_f32x16&>(*src); return; }
    if constexpr (::std::is_same_v<T,double> && N== 8) { reinterpret_cast<__vec_f64x8&>(*dst)  += reinterpret_cast<const __vec_f64x8&>(*src);  return; }
    if constexpr (::std::is_same_v<T,int>    && N==16) { reinterpret_cast<__vec_i32x16&>(*dst) += reinterpret_cast<const __vec_i32x16&>(*src); return; }
    if constexpr (::std::is_same_v<T,long>   && N== 8) { reinterpret_cast<__vec_i64x8&>(*dst)  += reinterpret_cast<const __vec_i64x8&>(*src);  return; }
#endif
#ifdef __SIMD_HAVE_256BIT
    if constexpr (::std::is_same_v<T,float>  && N==8) { reinterpret_cast<__vec_f32x8&>(*dst) += reinterpret_cast<const __vec_f32x8&>(*src); return; }
    if constexpr (::std::is_same_v<T,double> && N==4) { reinterpret_cast<__vec_f64x4&>(*dst) += reinterpret_cast<const __vec_f64x4&>(*src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==8) { reinterpret_cast<__vec_i32x8&>(*dst) += reinterpret_cast<const __vec_i32x8&>(*src); return; }
    if constexpr (::std::is_same_v<T,long>   && N==4) { reinterpret_cast<__vec_i64x4&>(*dst) += reinterpret_cast<const __vec_i64x4&>(*src); return; }
#endif
#ifdef __SIMD_HAVE_128BIT
    if constexpr (::std::is_same_v<T,float>  && N==4) { reinterpret_cast<__vec_f32x4&>(*dst) += reinterpret_cast<const __vec_f32x4&>(*src); return; }
    if constexpr (::std::is_same_v<T,double> && N==2) { reinterpret_cast<__vec_f64x2&>(*dst) += reinterpret_cast<const __vec_f64x2&>(*src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==4) { reinterpret_cast<__vec_i32x4&>(*dst) += reinterpret_cast<const __vec_i32x4&>(*src); return; }
    if constexpr (::std::is_same_v<T,long>   && N==2) { reinterpret_cast<__vec_i64x2&>(*dst) += reinterpret_cast<const __vec_i64x2&>(*src); return; }
#endif
    for (::std::size_t i = 0; i < N; ++i) dst[i] += src[i];
}

template<class T, ::std::size_t N>
inline void __simd_sub(T* __restrict__ dst, const T* __restrict__ src) noexcept {
    if (__SIMD_IS_CONSTEVAL()) { for (::std::size_t i=0;i<N;++i) dst[i]-=src[i]; return; }
#ifdef __SIMD_HAVE_512BIT
    if constexpr (::std::is_same_v<T,float>  && N==16) { reinterpret_cast<__vec_f32x16&>(*dst) -= reinterpret_cast<const __vec_f32x16&>(*src); return; }
    if constexpr (::std::is_same_v<T,double> && N== 8) { reinterpret_cast<__vec_f64x8&>(*dst)  -= reinterpret_cast<const __vec_f64x8&>(*src);  return; }
    if constexpr (::std::is_same_v<T,int>    && N==16) { reinterpret_cast<__vec_i32x16&>(*dst) -= reinterpret_cast<const __vec_i32x16&>(*src); return; }
#endif
#ifdef __SIMD_HAVE_256BIT
    if constexpr (::std::is_same_v<T,float>  && N==8) { reinterpret_cast<__vec_f32x8&>(*dst) -= reinterpret_cast<const __vec_f32x8&>(*src); return; }
    if constexpr (::std::is_same_v<T,double> && N==4) { reinterpret_cast<__vec_f64x4&>(*dst) -= reinterpret_cast<const __vec_f64x4&>(*src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==8) { reinterpret_cast<__vec_i32x8&>(*dst) -= reinterpret_cast<const __vec_i32x8&>(*src); return; }
#endif
#ifdef __SIMD_HAVE_128BIT
    if constexpr (::std::is_same_v<T,float>  && N==4) { reinterpret_cast<__vec_f32x4&>(*dst) -= reinterpret_cast<const __vec_f32x4&>(*src); return; }
    if constexpr (::std::is_same_v<T,double> && N==2) { reinterpret_cast<__vec_f64x2&>(*dst) -= reinterpret_cast<const __vec_f64x2&>(*src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==4) { reinterpret_cast<__vec_i32x4&>(*dst) -= reinterpret_cast<const __vec_i32x4&>(*src); return; }
#endif
    for (::std::size_t i = 0; i < N; ++i) dst[i] -= src[i];
}

template<class T, ::std::size_t N>
inline void __simd_mul(T* __restrict__ dst, const T* __restrict__ src) noexcept {
    if (__SIMD_IS_CONSTEVAL()) { for (::std::size_t i=0;i<N;++i) dst[i]*=src[i]; return; }
#ifdef __SIMD_HAVE_256BIT
    if constexpr (::std::is_same_v<T,float>  && N==8) { reinterpret_cast<__vec_f32x8&>(*dst) *= reinterpret_cast<const __vec_f32x8&>(*src); return; }
    if constexpr (::std::is_same_v<T,double> && N==4) { reinterpret_cast<__vec_f64x4&>(*dst) *= reinterpret_cast<const __vec_f64x4&>(*src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==8) { reinterpret_cast<__vec_i32x8&>(*dst) *= reinterpret_cast<const __vec_i32x8&>(*src); return; }
#endif
#ifdef __SIMD_HAVE_128BIT
    if constexpr (::std::is_same_v<T,float>  && N==4) { reinterpret_cast<__vec_f32x4&>(*dst) *= reinterpret_cast<const __vec_f32x4&>(*src); return; }
    if constexpr (::std::is_same_v<T,double> && N==2) { reinterpret_cast<__vec_f64x2&>(*dst) *= reinterpret_cast<const __vec_f64x2&>(*src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==4) { reinterpret_cast<__vec_i32x4&>(*dst) *= reinterpret_cast<const __vec_i32x4&>(*src); return; }
#endif
    for (::std::size_t i = 0; i < N; ++i) dst[i] *= src[i];
}

template<class T, ::std::size_t N>
inline void __simd_div(T* __restrict__ dst, const T* __restrict__ src) noexcept {
    if (__SIMD_IS_CONSTEVAL()) { for (::std::size_t i=0;i<N;++i) dst[i]/=src[i]; return; }
#ifdef __SIMD_HAVE_256BIT
    if constexpr (::std::is_same_v<T,float>  && N==8) { reinterpret_cast<__vec_f32x8&>(*dst) /= reinterpret_cast<const __vec_f32x8&>(*src); return; }
    if constexpr (::std::is_same_v<T,double> && N==4) { reinterpret_cast<__vec_f64x4&>(*dst) /= reinterpret_cast<const __vec_f64x4&>(*src); return; }
#endif
#ifdef __SIMD_HAVE_128BIT
    if constexpr (::std::is_same_v<T,float>  && N==4) { reinterpret_cast<__vec_f32x4&>(*dst) /= reinterpret_cast<const __vec_f32x4&>(*src); return; }
    if constexpr (::std::is_same_v<T,double> && N==2) { reinterpret_cast<__vec_f64x2&>(*dst) /= reinterpret_cast<const __vec_f64x2&>(*src); return; }
#endif
    for (::std::size_t i = 0; i < N; ++i) dst[i] /= src[i];
}

// ─── Reduce sum ───────────────────────────────────────────────────────────────
template<class T, ::std::size_t N>
inline T __simd_reduce_add(const T* __restrict__ src) noexcept {
    if (__SIMD_IS_CONSTEVAL()) {
        T acc = T{};
        for (::std::size_t i = 0; i < N; ++i) acc += src[i];
        return acc;
    }
#ifdef __SIMD_HAVE_256BIT
    if constexpr (::std::is_same_v<T,float>  && N==8) {
        __vec_f32x8 v; __builtin_memcpy(&v, src, sizeof(v));
        // Horizontal add: split into two halves, add, then reduce 4-wide
        __vec_f32x4 lo, hi;
        __builtin_memcpy(&lo, src,   sizeof(lo));
        __builtin_memcpy(&hi, src+4, sizeof(hi));
        __vec_f32x4 sum4 = lo + hi;
        float t[4]; __builtin_memcpy(t, &sum4, sizeof(t));
        return t[0]+t[1]+t[2]+t[3];
    }
    if constexpr (::std::is_same_v<T,double> && N==4) {
        __vec_f64x2 lo, hi;
        __builtin_memcpy(&lo, src,   sizeof(lo));
        __builtin_memcpy(&hi, src+2, sizeof(hi));
        __vec_f64x2 sum2 = lo + hi;
        double t[2]; __builtin_memcpy(t, &sum2, sizeof(t));
        return t[0]+t[1];
    }
#endif
#ifdef __SIMD_HAVE_128BIT
    if constexpr (::std::is_same_v<T,float>  && N==4) {
        __vec_f32x4 v; __builtin_memcpy(&v, src, sizeof(v));
        float t[4]; __builtin_memcpy(t, &v, sizeof(t));
        return t[0]+t[1]+t[2]+t[3];
    }
    if constexpr (::std::is_same_v<T,double> && N==2) {
        __vec_f64x2 v; __builtin_memcpy(&v, src, sizeof(v));
        double t[2]; __builtin_memcpy(t, &v, sizeof(t));
        return t[0]+t[1];
    }
#endif
    T acc = T{};
    for (::std::size_t i = 0; i < N; ++i) acc += src[i];
    return acc;
}

} // namespace detail
