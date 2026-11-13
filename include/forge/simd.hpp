#pragma once

#include <simd>

namespace forge {

// Deduce ABI types through the standard aliases, including native stand-aside.
template<class T>
using simd_native_abi = typename std::simd::vec<T>::abi_type;

template<class T, decltype(std::simd::vec<T>::size()) N>
using simd_abi = typename std::simd::vec<T, N>::abi_type;

#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED)
// These explicit implementation tags have no portable native equivalent.
template<std::simd::__simd_size_type N>
using simd_fixed_size_abi = std::simd::__fixed_size_abi<N>;

template<class T, std::simd::__simd_size_type N>
using simd_native_abi_tag = std::simd::__detail::__native_abi_tag<T, N>;
#endif

} // namespace forge
