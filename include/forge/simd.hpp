#pragma once

#include <simd>

namespace forge {

// Deduce ABI types through the standard aliases, including native stand-aside.
template<class T>
using simd_native_abi = typename std::simd::vec<T>::abi_type;

template<class T, decltype(std::simd::vec<T>::size()) N>
using simd_abi = typename std::simd::vec<T, N>::abi_type;

} // namespace forge
