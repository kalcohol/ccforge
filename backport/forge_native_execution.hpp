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
#define FORGE_HAS_NATIVE_EXECUTION_POLICIES 1
#elif defined(__has_include_next)
#if __has_include_next(<execution>)
#include_next <execution>
#define FORGE_HAS_NATIVE_EXECUTION_POLICIES 1
#endif
#endif

// Header existence alone does not establish the availability of policy objects.
#if defined(FORGE_HAS_NATIVE_EXECUTION_POLICIES) && \
    defined(__cpp_lib_execution) && __cpp_lib_execution >= 201603L
#define FORGE_NATIVE_EXECUTION_POLICY_OBJECTS 1
#else
#define FORGE_NATIVE_EXECUTION_POLICY_OBJECTS 0
#endif
