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
#define FORGE_SIMD_DETAIL_HAVE_128BIT 1
#endif

#if defined(__AVX2__) || defined(__AVX__)
typedef float  __attribute__((vector_size(32))) __vec_f32x8;
typedef double __attribute__((vector_size(32))) __vec_f64x4;
typedef int    __attribute__((vector_size(32))) __vec_i32x8;
typedef long   __attribute__((vector_size(32))) __vec_i64x4;
#define FORGE_SIMD_DETAIL_HAVE_256BIT 1
#endif

#if defined(__AVX512F__)
typedef float  __attribute__((vector_size(64))) __vec_f32x16;
typedef double __attribute__((vector_size(64))) __vec_f64x8;
typedef int    __attribute__((vector_size(64))) __vec_i32x16;
typedef long   __attribute__((vector_size(64))) __vec_i64x8;
#define FORGE_SIMD_DETAIL_HAVE_512BIT 1
#endif

// ─── is_constant_evaluated helper ────────────────────────────────────────────
// __builtin_is_constant_evaluated() is GCC 9+/Clang 9+

#if defined(__has_builtin) && __has_builtin(__builtin_is_constant_evaluated)
#define FORGE_SIMD_DETAIL_IS_CONSTEVAL() __builtin_is_constant_evaluated()
#elif __cpp_lib_is_constant_evaluated >= 201811L
#include <utility>
#define FORGE_SIMD_DETAIL_IS_CONSTEVAL() ::std::is_constant_evaluated()
#else
#define FORGE_SIMD_DETAIL_IS_CONSTEVAL() false
#endif

// ─── Vector add/sub/mul helpers ──────────────────────────────────────────────

template<class Vec, class T>
inline void __simd_add_vec(T* dst, const T* src) noexcept {
    Vec left;
    Vec right;
    __builtin_memcpy(&left, dst, sizeof(left));
    __builtin_memcpy(&right, src, sizeof(right));
    left += right;
    __builtin_memcpy(dst, &left, sizeof(left));
}

template<class Vec, class T>
inline void __simd_sub_vec(T* dst, const T* src) noexcept {
    Vec left;
    Vec right;
    __builtin_memcpy(&left, dst, sizeof(left));
    __builtin_memcpy(&right, src, sizeof(right));
    left -= right;
    __builtin_memcpy(dst, &left, sizeof(left));
}

template<class Vec, class T>
inline void __simd_mul_vec(T* dst, const T* src) noexcept {
    Vec left;
    Vec right;
    __builtin_memcpy(&left, dst, sizeof(left));
    __builtin_memcpy(&right, src, sizeof(right));
    left *= right;
    __builtin_memcpy(dst, &left, sizeof(left));
}

template<class Vec, class T>
inline void __simd_div_vec(T* dst, const T* src) noexcept {
    Vec left;
    Vec right;
    __builtin_memcpy(&left, dst, sizeof(left));
    __builtin_memcpy(&right, src, sizeof(right));
    left /= right;
    __builtin_memcpy(dst, &left, sizeof(left));
}

template<class T, ::std::size_t N>
inline void __simd_add(T* dst, const T* src) noexcept {
    if (FORGE_SIMD_DETAIL_IS_CONSTEVAL()) { for (::std::size_t i=0;i<N;++i) dst[i]+=src[i]; return; }
#ifdef FORGE_SIMD_DETAIL_HAVE_512BIT
    if constexpr (::std::is_same_v<T,float>  && N==16) { __simd_add_vec<__vec_f32x16>(dst, src); return; }
    if constexpr (::std::is_same_v<T,double> && N== 8) { __simd_add_vec<__vec_f64x8>(dst, src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==16) { __simd_add_vec<__vec_i32x16>(dst, src); return; }
    if constexpr (::std::is_same_v<T,long>   && sizeof(long)==8 && N==8) { __simd_add_vec<__vec_i64x8>(dst, src); return; }
#endif
#ifdef FORGE_SIMD_DETAIL_HAVE_256BIT
    if constexpr (::std::is_same_v<T,float>  && N==8) { __simd_add_vec<__vec_f32x8>(dst, src); return; }
    if constexpr (::std::is_same_v<T,double> && N==4) { __simd_add_vec<__vec_f64x4>(dst, src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==8) { __simd_add_vec<__vec_i32x8>(dst, src); return; }
    if constexpr (::std::is_same_v<T,long>   && sizeof(long)==8 && N==4) { __simd_add_vec<__vec_i64x4>(dst, src); return; }
#endif
#ifdef FORGE_SIMD_DETAIL_HAVE_128BIT
    if constexpr (::std::is_same_v<T,float>  && N==4) { __simd_add_vec<__vec_f32x4>(dst, src); return; }
    if constexpr (::std::is_same_v<T,double> && N==2) { __simd_add_vec<__vec_f64x2>(dst, src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==4) { __simd_add_vec<__vec_i32x4>(dst, src); return; }
    if constexpr (::std::is_same_v<T,long>   && sizeof(long)==8 && N==2) { __simd_add_vec<__vec_i64x2>(dst, src); return; }
#endif
    for (::std::size_t i = 0; i < N; ++i) dst[i] += src[i];
}

template<class T, ::std::size_t N>
inline void __simd_sub(T* dst, const T* src) noexcept {
    if (FORGE_SIMD_DETAIL_IS_CONSTEVAL()) { for (::std::size_t i=0;i<N;++i) dst[i]-=src[i]; return; }
#ifdef FORGE_SIMD_DETAIL_HAVE_512BIT
    if constexpr (::std::is_same_v<T,float>  && N==16) { __simd_sub_vec<__vec_f32x16>(dst, src); return; }
    if constexpr (::std::is_same_v<T,double> && N== 8) { __simd_sub_vec<__vec_f64x8>(dst, src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==16) { __simd_sub_vec<__vec_i32x16>(dst, src); return; }
#endif
#ifdef FORGE_SIMD_DETAIL_HAVE_256BIT
    if constexpr (::std::is_same_v<T,float>  && N==8) { __simd_sub_vec<__vec_f32x8>(dst, src); return; }
    if constexpr (::std::is_same_v<T,double> && N==4) { __simd_sub_vec<__vec_f64x4>(dst, src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==8) { __simd_sub_vec<__vec_i32x8>(dst, src); return; }
#endif
#ifdef FORGE_SIMD_DETAIL_HAVE_128BIT
    if constexpr (::std::is_same_v<T,float>  && N==4) { __simd_sub_vec<__vec_f32x4>(dst, src); return; }
    if constexpr (::std::is_same_v<T,double> && N==2) { __simd_sub_vec<__vec_f64x2>(dst, src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==4) { __simd_sub_vec<__vec_i32x4>(dst, src); return; }
#endif
    for (::std::size_t i = 0; i < N; ++i) dst[i] -= src[i];
}

template<class T, ::std::size_t N>
inline void __simd_mul(T* dst, const T* src) noexcept {
    if (FORGE_SIMD_DETAIL_IS_CONSTEVAL()) { for (::std::size_t i=0;i<N;++i) dst[i]*=src[i]; return; }
#ifdef FORGE_SIMD_DETAIL_HAVE_256BIT
    if constexpr (::std::is_same_v<T,float>  && N==8) { __simd_mul_vec<__vec_f32x8>(dst, src); return; }
    if constexpr (::std::is_same_v<T,double> && N==4) { __simd_mul_vec<__vec_f64x4>(dst, src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==8) { __simd_mul_vec<__vec_i32x8>(dst, src); return; }
#endif
#ifdef FORGE_SIMD_DETAIL_HAVE_128BIT
    if constexpr (::std::is_same_v<T,float>  && N==4) { __simd_mul_vec<__vec_f32x4>(dst, src); return; }
    if constexpr (::std::is_same_v<T,double> && N==2) { __simd_mul_vec<__vec_f64x2>(dst, src); return; }
    if constexpr (::std::is_same_v<T,int>    && N==4) { __simd_mul_vec<__vec_i32x4>(dst, src); return; }
#endif
    for (::std::size_t i = 0; i < N; ++i) dst[i] *= src[i];
}

template<class T, ::std::size_t N>
inline void __simd_div(T* dst, const T* src) noexcept {
    if (FORGE_SIMD_DETAIL_IS_CONSTEVAL()) { for (::std::size_t i=0;i<N;++i) dst[i]/=src[i]; return; }
#ifdef FORGE_SIMD_DETAIL_HAVE_256BIT
    if constexpr (::std::is_same_v<T,float>  && N==8) { __simd_div_vec<__vec_f32x8>(dst, src); return; }
    if constexpr (::std::is_same_v<T,double> && N==4) { __simd_div_vec<__vec_f64x4>(dst, src); return; }
#endif
#ifdef FORGE_SIMD_DETAIL_HAVE_128BIT
    if constexpr (::std::is_same_v<T,float>  && N==4) { __simd_div_vec<__vec_f32x4>(dst, src); return; }
    if constexpr (::std::is_same_v<T,double> && N==2) { __simd_div_vec<__vec_f64x2>(dst, src); return; }
#endif
    for (::std::size_t i = 0; i < N; ++i) dst[i] /= src[i];
}

} // namespace detail

#undef FORGE_SIMD_DETAIL_IS_CONSTEVAL
#undef FORGE_SIMD_DETAIL_HAVE_128BIT
#undef FORGE_SIMD_DETAIL_HAVE_256BIT
#undef FORGE_SIMD_DETAIL_HAVE_512BIT
