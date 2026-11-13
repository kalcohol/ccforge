#include <forge/simd.hpp>
#include <type_traits>

// These checks concern alias semantics, not deprecation diagnostics.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

struct contract_value {};
struct contract_abi {};

#if FORGE_SIMD_ALIAS_SPECIALIZATION_CASE == 1
template<>
struct std::simd::simd_size<contract_value, contract_abi> {};
#elif FORGE_SIMD_ALIAS_SPECIALIZATION_CASE == 2
template<class T>
struct std::simd::simd_size<T, contract_abi> {};
#else
// Explicitly passing the alias as a template argument is supported. Deduction
// from an instance identifies the reserved class template instead of the alias.
template<template<class, class> class Trait>
using apply_trait = Trait<int, forge::simd_fixed_size_abi<4>>;
static_assert(apply_trait<std::simd::simd_size>::value == 4);
template<template<class, class> class>
struct template_identity {};
template<template<class, class> class Trait, class T, class Abi>
constexpr bool deduces_reserved(Trait<T, Abi>*) {
    return std::is_same_v<template_identity<Trait>, template_identity<std::simd::__simd_size>>;
}
static_assert(deduces_reserved(static_cast<apply_trait<std::simd::simd_size>*>(nullptr)));
#endif

#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

int main() {}
