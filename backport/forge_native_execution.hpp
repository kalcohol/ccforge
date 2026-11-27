// MIT License
//
// Copyright (c) 2026 Forge Project
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

// Shared native-only entry point. Include this with angle brackets so that
// include_next starts after the backport include root, not cpp26/linalg/.
#if defined(_MSC_VER)
#ifndef FORGE_MSVC_EXECUTION_HEADER
#error "FORGE_MSVC_EXECUTION_HEADER is required when Forge backport is enabled on MSVC"
#endif
#include FORGE_MSVC_EXECUTION_HEADER
#define FORGE_DETAIL_HAS_NATIVE_EXECUTION_HEADER 1
#elif defined(__has_include_next)
#if __has_include_next(<execution>)
#include_next <execution>
#define FORGE_DETAIL_HAS_NATIVE_EXECUTION_HEADER 1
#endif
#endif

// One decision per translation unit, taken from the native library's own
// configuration after its header has been included:
//   FORGE_NATIVE_EXECUTION_POLICY_OBJECTS  seq, par, par_unseq and their types
//   FORGE_NATIVE_EXECUTION_POLICY_UNSEQ    unseq and unsequenced_policy (C++20)
// Both are always defined, as 0 or 1, and UNSEQ implies OBJECTS.
//
// libc++ always ships <execution> but declares the policies only when its
// PSTL is enabled (-fexperimental-library since libc++ 17), and it does not
// define __cpp_lib_execution even then. From libc++ 17 on, each branch repeats
// the guard of that release's own header. libstdc++ and the MSVC STL declare
// the policies exactly when they define __cpp_lib_execution.
//
// The level is 0 without policies, 1 with the C++17 objects, 2 with unseq.
#if !defined(FORGE_DETAIL_HAS_NATIVE_EXECUTION_HEADER)
#define FORGE_DETAIL_NATIVE_POLICY_LEVEL 0
#elif defined(_LIBCPP_VERSION)
#if _LIBCPP_STD_VER < 17
#define FORGE_DETAIL_NATIVE_POLICY_LEVEL 0
#elif _LIBCPP_VERSION >= 170000
// libc++ 20 and later always define _LIBCPP_HAS_EXPERIMENTAL_PSTL, as 0 or 1.
// libc++ 17 to 19 define _LIBCPP_HAS_NO_INCOMPLETE_PSTL when it is disabled.
#if (_LIBCPP_VERSION >= 200000 && defined(_LIBCPP_HAS_EXPERIMENTAL_PSTL) && _LIBCPP_HAS_EXPERIMENTAL_PSTL) || \
    (_LIBCPP_VERSION < 200000 && !defined(_LIBCPP_HAS_NO_INCOMPLETE_PSTL))
#if _LIBCPP_STD_VER >= 20
#define FORGE_DETAIL_NATIVE_POLICY_LEVEL 2
#else
#define FORGE_DETAIL_NATIVE_POLICY_LEVEL 1
#endif
#else
#define FORGE_DETAIL_NATIVE_POLICY_LEVEL 0
#endif
#else
// Floor: libc++ 16 and earlier report no policies, even when built with the
// external PSTL (_LIBCPP_HAS_PARALLEL_ALGORITHMS). No verification lane has
// such a library, and the conservative answer only disables policy overloads.
#define FORGE_DETAIL_NATIVE_POLICY_LEVEL 0
#endif
#elif defined(__cpp_lib_execution) && __cpp_lib_execution >= 201902L
#define FORGE_DETAIL_NATIVE_POLICY_LEVEL 2
#elif defined(__cpp_lib_execution) && __cpp_lib_execution >= 201603L
#define FORGE_DETAIL_NATIVE_POLICY_LEVEL 1
#else
#define FORGE_DETAIL_NATIVE_POLICY_LEVEL 0
#endif

#if FORGE_DETAIL_NATIVE_POLICY_LEVEL >= 1
#define FORGE_NATIVE_EXECUTION_POLICY_OBJECTS 1
#else
#define FORGE_NATIVE_EXECUTION_POLICY_OBJECTS 0
#endif
#if FORGE_DETAIL_NATIVE_POLICY_LEVEL >= 2
#define FORGE_NATIVE_EXECUTION_POLICY_UNSEQ 1
#else
#define FORGE_NATIVE_EXECUTION_POLICY_UNSEQ 0
#endif

#undef FORGE_DETAIL_NATIVE_POLICY_LEVEL
#undef FORGE_DETAIL_HAS_NATIVE_EXECUTION_HEADER
