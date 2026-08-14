# Backport feature probes for forge.cmake.
#
# This file expects the standard-header target to already exist. Set
# FORGE_BACKPORT_TARGET before including this file; it defaults to forge for
# compatibility with older direct include patterns. It sets:
#   FORGE_NEEDS_BACKPORT
#   FORGE_NEEDS_EXPERIMENTAL
# and publishes FORGE_HAS_NATIVE_* / FORGE_FORCE_* definitions on that target.

include(CheckCXXSourceCompiles)
include("${CMAKE_CURRENT_LIST_DIR}/ForgeProbeFingerprint.cmake")

set(FORGE_NEEDS_BACKPORT FALSE)

if(NOT DEFINED FORGE_BACKPORT_TARGET)
    set(FORGE_BACKPORT_TARGET forge)
endif()
if(NOT TARGET ${FORGE_BACKPORT_TARGET})
    message(FATAL_ERROR "CC Forge: FORGE_BACKPORT_TARGET '${FORGE_BACKPORT_TARGET}' does not exist")
endif()

# Probe at the SAME language standard the consumer compiles with. Detecting a
# C++26 feature that is only reachable under -std=c++26 is meaningless if the
# project itself builds at C++23, so native presence must be judged at the build
# standard, not the compiler max.
if(DEFINED CMAKE_CXX_STANDARD)
    set(_forge_std "${CMAKE_CXX_STANDARD}")
else()
    set(_forge_std 23)
endif()

_forge_compute_probe_fingerprint(_forge_probe_fingerprint "${_forge_std}")
if(DEFINED FORGE_BACKPORT_PROBE_FINGERPRINT
        AND NOT "${FORGE_BACKPORT_PROBE_FINGERPRINT}" STREQUAL "${_forge_probe_fingerprint}")
    foreach(_forge_probe_var
            HAS_STD_UNIQUE_RESOURCE
            FORGE_SIMD_FULL
            FORGE_SIMD_PARTIAL_MACRO
            FORGE_SIMD_PARTIAL_VEC
            FORGE_SIMD_PARTIAL_MASK
            FORGE_SENDERS_FULL
            FORGE_SENDERS_PARTIAL
            FORGE_CONSTANT_WRAPPER_FULL
            FORGE_CONSTANT_WRAPPER_PARTIAL
            FORGE_BASE_MDSPAN_AVAILABLE
            FORGE_MDSPAN_PADDED_LAYOUTS_FULL
            FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_LEFT
            FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_RIGHT
            FORGE_SUBMDSPAN_FULL
            FORGE_SUBMDSPAN_PARTIAL_MACRO
            FORGE_SUBMDSPAN_PARTIAL_CURRENT
            FORGE_SUBMDSPAN_PARTIAL_LEGACY
            FORGE_SUBMDSPAN_PARTIAL_MAPPING_RESULT
            FORGE_SUBMDSPAN_PARTIAL_FUNCTION
            FORGE_LINALG_FULL
            FORGE_LINALG_PARTIAL_MACRO
            FORGE_LINALG_PARTIAL_SETUP
            FORGE_LINALG_PARTIAL_TAG
            FORGE_LINALG_PARTIAL_LAYOUT
            FORGE_LINALG_PARTIAL_MATRIX_PRODUCT)
        unset(${_forge_probe_var} CACHE)
        unset(${_forge_probe_var})
    endforeach()
endif()
set(FORGE_BACKPORT_PROBE_FINGERPRINT "${_forge_probe_fingerprint}"
    CACHE INTERNAL "CC Forge backport probe fingerprint")

set(_forge_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
if(MSVC)
    set(_forge_msvc_standard_option_var
        "CMAKE_CXX${_forge_std}_STANDARD_COMPILE_OPTION")
    if(DEFINED ${_forge_msvc_standard_option_var}
            AND NOT "${${_forge_msvc_standard_option_var}}" STREQUAL "")
        set(_forge_msvc_standard_option
            "${${_forge_msvc_standard_option_var}}")
    elseif(_forge_std GREATER_EQUAL 23)
        # MSVC versions that provide C++23 through CMake may expose it only
        # through /std:c++latest; /std:c++23 is not a portable MSVC option.
        set(_forge_msvc_standard_option "/std:c++latest")
    else()
        set(_forge_msvc_standard_option "/std:c++${_forge_std}")
    endif()
    set(CMAKE_REQUIRED_FLAGS
        "${CMAKE_REQUIRED_FLAGS} ${_forge_msvc_standard_option} /Zc:__cplusplus")
    unset(_forge_msvc_standard_option)
    unset(_forge_msvc_standard_option_var)
else()
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++${_forge_std}")
endif()

# Three-state decision helper for each backportable feature.
#
#   full    = the COMPLETE required API surface compiles natively
#   partial = ANY trace of a native implementation is present
#
#   full             -> native is complete: stand aside, signal the wrapper.
#   !full && partial -> native is incomplete: stand aside anyway to avoid
#                       injecting the backport on top of partial native
#                       declarations in namespace std (ODR violation), and warn.
#   !full && !partial -> no native at all: inject the backport.
#
# FORGE_FORCE_<F>_BACKPORT overrides the decision and forces injection
# (UB-prone on a partial-native toolchain; used only for testing/diagnosis).
macro(_forge_decide _disp _suffix _full _partial)
    if(FORGE_FORCE_${_suffix}_BACKPORT)
        set(FORGE_NEEDS_BACKPORT TRUE)
        target_compile_definitions(${FORGE_BACKPORT_TARGET} INTERFACE FORGE_FORCE_${_suffix}_BACKPORT=1)
        message(STATUS "CC Forge probe: ${_suffix}=FORCED")
        message(WARNING "CC Forge: ${_disp} backport FORCED on (FORGE_FORCE_${_suffix}_BACKPORT=ON); injecting on top of any native implementation risks ODR violations")
    elseif(${_full})
        target_compile_definitions(${FORGE_BACKPORT_TARGET} INTERFACE FORGE_HAS_NATIVE_${_suffix}=1)
        message(STATUS "CC Forge probe: ${_suffix}=COMPLETE")
        message(STATUS "CC Forge: ${_disp} native support detected - backport disabled")
    elseif(${_partial})
        target_compile_definitions(${FORGE_BACKPORT_TARGET} INTERFACE FORGE_HAS_NATIVE_${_suffix}=1)
        message(STATUS "CC Forge probe: ${_suffix}=PARTIAL")
        message(WARNING "CC Forge: ${_disp} native support is present but INCOMPLETE at -std=c++${_forge_std}; Forge stands aside to avoid ODR conflicts. Wait for the toolchain to finish it or select a language mode where these declarations are absent. FORGE_FORCE_${_suffix}_BACKPORT remains a diagnostic-only switch and may fail to compile or violate the ODR on this toolchain.")
    else()
        set(FORGE_NEEDS_BACKPORT TRUE)
        message(STATUS "CC Forge probe: ${_suffix}=BACKPORT")
        message(STATUS "CC Forge: ${_disp} backport enabled")
    endif()
endmacro()

option(FORGE_FORCE_SIMD_BACKPORT "Force the std::simd backport even if (partial) native support exists" OFF)
option(FORGE_FORCE_SENDERS_BACKPORT "Force the std::execution backport even if (partial) native support exists" OFF)
option(FORGE_FORCE_CONSTANT_WRAPPER_BACKPORT "Force the std::constant_wrapper backport even if (partial) native support exists" OFF)
option(FORGE_FORCE_MDSPAN_PADDED_LAYOUTS_BACKPORT "Force the std::mdspan padded-layout backport even if (partial) native support exists" OFF)
option(FORGE_FORCE_SUBMDSPAN_BACKPORT "Force the std::submdspan backport even if (partial) native support exists" OFF)
option(FORGE_FORCE_LINALG_BACKPORT "Force the std::linalg backport even if (partial) native support exists" OFF)

# std::unique_resource is TS v3, not C++26.
check_cxx_source_compiles("
    #include <version>
    #if !defined(__cpp_lib_unique_resource) || __cpp_lib_unique_resource < 202311L
    #error unique_resource not available
    #endif
    int main() { return 0; }
" HAS_STD_UNIQUE_RESOURCE)

set(FORGE_NEEDS_EXPERIMENTAL FALSE)
if(NOT HAS_STD_UNIQUE_RESOURCE)
    set(FORGE_NEEDS_EXPERIMENTAL TRUE)
    message(STATUS "CC Forge: std::unique_resource backport enabled (TS v3)")
endif()

# std::simd (P1928). Probe a core declaration rather than header presence:
# some libraries install an empty <simd> outside C++26 mode.
check_cxx_source_compiles("
    #include <simd>
    #include <array>
    #include <bit>
    #include <cmath>
    #include <complex>
    #include <span>
    #if !defined(__cpp_lib_simd) || __cpp_lib_simd < 202606L
    #error incomplete simd core surface
    #endif
    #if !defined(__cpp_lib_simd_bitops) || __cpp_lib_simd_bitops < 202607L
    #error incomplete simd bit surface
    #endif
    #if !defined(__cpp_lib_simd_complex) || __cpp_lib_simd_complex < 202502L
    #error incomplete simd complex surface
    #endif
    #if !defined(__cpp_lib_simd_permutations) || __cpp_lib_simd_permutations < 202506L
    #error incomplete simd permutation surface
    #endif
    int main() {
        using float4 = std::simd::vec<float, 4>;
        using uint4 = std::simd::vec<unsigned, 4>;
        using complex4 = std::simd::vec<std::complex<float>, 4>;
        float4 v(1.0f);
        uint4 bits(1u);
        auto sum = std::simd::reduce(v);
        auto mask = v == v;
        auto math = std::simd::sin(v);
        auto top_level_math = std::sin(v);
        auto counted = std::simd::popcount(bits);
        auto top_level_counted = std::popcount(bits);
        complex4 complex_values(float4(1.0f), float4(2.0f));
        auto real_values = std::simd::real(complex_values);
        auto sequence = std::simd::iota<float4>;
        std::array<float, 8> input{};
        std::array<float, 8> output{};
        std::span<const float, 8> input_view(input);
        std::simd::vec<int, 4> indices(0);
        auto loaded = std::simd::partial_load<float4>(input.data(), 4);
        std::simd::partial_store(loaded, output.data(), 4);
        auto unchecked = std::simd::unchecked_load<float4>(input.data());
        std::simd::unchecked_store(unchecked, output.data());
        auto gathered = std::simd::partial_gather_from(input_view, indices);
        std::simd::partial_scatter_to(gathered, output.data(), 8, indices);
        auto permuted = std::simd::permute(v, indices);
        return static_cast<int>(
            sum + std::simd::reduce_count(mask) + math[0] +
            top_level_math[0] + static_cast<float>(counted[0]) +
            static_cast<float>(top_level_counted[0]) + real_values[0] +
            sequence[0] + gathered[0] + permuted[0] + output[0]);
    }
" FORGE_SIMD_FULL)
check_cxx_source_compiles("
    #include <simd>
    #ifndef __cpp_lib_simd
    #error no simd feature macro
    #endif
    int main() { return 0; }
" FORGE_SIMD_PARTIAL_MACRO)
check_cxx_source_compiles("
    #include <simd>
    using probe = std::simd::basic_vec<float>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SIMD_PARTIAL_VEC)
check_cxx_source_compiles("
    #include <simd>
    #include <cstddef>
    template<template<std::size_t, class> class>
    struct accepts_mask_template {};
    using probe = accepts_mask_template<std::simd::basic_mask>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SIMD_PARTIAL_MASK)
set(FORGE_SIMD_PARTIAL FALSE)
if(FORGE_SIMD_PARTIAL_MACRO OR FORGE_SIMD_PARTIAL_VEC OR FORGE_SIMD_PARTIAL_MASK)
    set(FORGE_SIMD_PARTIAL TRUE)
endif()
_forge_decide("std::simd" SIMD FORGE_SIMD_FULL FORGE_SIMD_PARTIAL)

# P2300 senders/receivers under <execution>. <execution> always exists for
# C++17 parallel policies, so it is not a discriminator; probe for P2300 API.
check_cxx_source_compiles("
    #include <execution>
    #include <tuple>
    int main() {
        auto s = std::execution::just(1);
        auto r = std::execution::sync_wait(s);
        return r ? std::get<0>(*r) : 0;
    }
" FORGE_SENDERS_FULL)
check_cxx_source_compiles("
    #include <execution>
    int main() {
        auto s = std::execution::just(1);
        (void)s;
        return 0;
    }
" FORGE_SENDERS_PARTIAL)
_forge_decide("std::execution (P2300 senders/receivers)" SENDERS FORGE_SENDERS_FULL FORGE_SENDERS_PARTIAL)

# std::constant_wrapper (P2781 + P3978 + P4206). submdspan's current C++26
# surface uses it directly, so it needs its own stand-aside guard. P4206 is a
# C++26 DR and bumps the feature-test macro to 202606L; older 202603L native
# implementations are partial and must still make Forge stand aside.
check_cxx_source_compiles("
    #include <utility>
    #include <type_traits>

    #if !defined(__cpp_lib_constant_wrapper) || __cpp_lib_constant_wrapper < 202606L
    #error incomplete constant_wrapper
    #endif

    constexpr int plus_one(int value) { return value + 1; }
    struct lookup {
        int values[2];
        constexpr int operator[](std::size_t index) const { return values[index]; }
    };

    int main() {
        using one = std::constant_wrapper<1zu>;
        static_assert(one::value == 1zu);
        static_assert(std::is_same_v<
            std::remove_cv_t<decltype(std::cw<2zu>)>,
            std::constant_wrapper<2zu>>);
        static_assert(std::is_same_v<
            decltype(std::cw<1> + std::integral_constant<int, 2>{}),
            std::constant_wrapper<3>>);
        static_assert(std::is_same_v<
            decltype(std::cw<&plus_one>(std::cw<2>)),
            std::constant_wrapper<3>>);
        static_assert(std::is_same_v<
            decltype(std::cw<lookup{{4, 5}}>[std::cw<1zu>]),
            std::constant_wrapper<5>>);
        static_assert(std::is_same_v<
            decltype(++std::cw<1>),
            std::constant_wrapper<2>>);
        return static_cast<int>(one{});
    }
" FORGE_CONSTANT_WRAPPER_FULL)
check_cxx_source_compiles("
    #include <utility>
    using probe = std::constant_wrapper<1zu>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_CONSTANT_WRAPPER_PARTIAL)
_forge_decide("std::constant_wrapper" CONSTANT_WRAPPER FORGE_CONSTANT_WRAPPER_FULL FORGE_CONSTANT_WRAPPER_PARTIAL)

# The mdspan-dependent backports extend a toolchain-provided base <mdspan>;
# CC Forge does not ship the C++23 mdspan foundation itself.
check_cxx_source_compiles("
    #include <mdspan>
    int main() {
        int data[1]{};
        std::mdspan<int, std::extents<int, 1>> view(data);
        return view[0];
    }
" FORGE_BASE_MDSPAN_AVAILABLE)

if(FORGE_BASE_MDSPAN_AVAILABLE)
# C++26 mdspan padded layouts (P2642). These are not useful only to submdspan,
# so they get a native guard before the submdspan wrapper can inject mappings.
check_cxx_source_compiles("
    #include <mdspan>
    #if !defined(__cpp_lib_mdspan) || __cpp_lib_mdspan < 202406L
    #error incomplete mdspan padded layouts
    #endif
    int main() {
        using ext_t = std::extents<int, 3, 4>;
        ext_t e;
        using dynamic_left = std::layout_left_padded<>;
        using dynamic_right = std::layout_right_padded<>;
        std::layout_left_padded<8>::mapping<ext_t> left(e, 8);
        std::layout_right_padded<8>::mapping<ext_t> right(e, 8);
        dynamic_left::mapping<ext_t> left_dynamic(e, 8);
        dynamic_right::mapping<ext_t> right_dynamic(e, 8);
        static_assert(decltype(left)::padding_value == 8);
        static_assert(decltype(right)::padding_value == 8);
        static_assert(decltype(left_dynamic)::padding_value == std::dynamic_extent);
        static_assert(decltype(right_dynamic)::padding_value == std::dynamic_extent);
        static_assert(decltype(left)::is_always_unique());
        static_assert(decltype(left)::is_always_strided());
        static_assert(decltype(right)::is_always_unique());
        static_assert(decltype(right)::is_always_strided());
        return static_cast<int>(
            left.stride(1) + right.stride(0) +
            left_dynamic.stride(1) + right_dynamic.stride(0) +
            left.required_span_size() + right.required_span_size() +
            left(1, 1) + right(1, 1) +
            left.is_unique() + right.is_strided());
    }
" FORGE_MDSPAN_PADDED_LAYOUTS_FULL)
check_cxx_source_compiles("
    #include <mdspan>
    #include <cstddef>
    template<template<std::size_t> class>
    struct accepts_layout_template {};
    using probe = accepts_layout_template<std::layout_left_padded>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_LEFT)
check_cxx_source_compiles("
    #include <mdspan>
    #include <cstddef>
    template<template<std::size_t> class>
    struct accepts_layout_template {};
    using probe = accepts_layout_template<std::layout_right_padded>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_RIGHT)
set(FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL FALSE)
if(FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_LEFT
        OR FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL_RIGHT)
    set(FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL TRUE)
endif()
_forge_decide("std::mdspan padded layouts" MDSPAN_PADDED_LAYOUTS FORGE_MDSPAN_PADDED_LAYOUTS_FULL FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL)

# std::submdspan (P2630 + P3663/P3982 wording) is only meaningful when <mdspan>
# exists. Partial probes must look for submdspan-specific symbols: <mdspan> alone
# (for example GCC 14) has no submdspan and must still get the backport.
check_cxx_source_compiles("
    #include <mdspan>
    #include <tuple>
    int main() {
        int data[12]{};
        std::mdspan<int, std::extents<int, 3, 4>> m(data);
        auto sub = std::submdspan(m, 1, std::full_extent);
        auto e = std::subextents(m.extents(), std::full_extent, std::range_slice{0, 4, 2});
        auto c = std::canonical_slices(m.extents(), std::full_extent, std::range_slice{0, 4, 2});
        using es = std::extent_slice<int, int, int>;
        using rs = std::range_slice<int, int>;
        static_assert(__cpp_lib_submdspan >= 202603L);
        (void)sub; (void)e; (void)c; (void)sizeof(es); (void)sizeof(rs);
        return 0;
    }
" FORGE_SUBMDSPAN_FULL)
check_cxx_source_compiles("
    #include <mdspan>
    #ifndef __cpp_lib_submdspan
    #error no submdspan feature macro
    #endif
    int main() { return 0; }
" FORGE_SUBMDSPAN_PARTIAL_MACRO)
check_cxx_source_compiles("
    #include <mdspan>
    using probe = std::extent_slice<int, int, int>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SUBMDSPAN_PARTIAL_CURRENT)
check_cxx_source_compiles("
    #include <mdspan>
    using probe = std::strided_slice<int, int, int>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SUBMDSPAN_PARTIAL_LEGACY)
check_cxx_source_compiles("
    #include <mdspan>
    template<template<class> class>
    struct accepts_result_template {};
    using probe = accepts_result_template<std::submdspan_mapping_result>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SUBMDSPAN_PARTIAL_MAPPING_RESULT)
check_cxx_source_compiles("
    #include <mdspan>
    int main() {
        int data[1]{};
        std::mdspan<int, std::extents<int, 1>> view(data);
        auto sub = std::submdspan(view, std::full_extent);
        (void)sub;
        return 0;
    }
" FORGE_SUBMDSPAN_PARTIAL_FUNCTION)
set(FORGE_SUBMDSPAN_PARTIAL FALSE)
if(FORGE_SUBMDSPAN_PARTIAL_MACRO
        OR FORGE_SUBMDSPAN_PARTIAL_CURRENT
        OR FORGE_SUBMDSPAN_PARTIAL_LEGACY
        OR FORGE_SUBMDSPAN_PARTIAL_MAPPING_RESULT
        OR FORGE_SUBMDSPAN_PARTIAL_FUNCTION)
    set(FORGE_SUBMDSPAN_PARTIAL TRUE)
endif()
_forge_decide("std::submdspan" SUBMDSPAN FORGE_SUBMDSPAN_FULL FORGE_SUBMDSPAN_PARTIAL)

# std::linalg (P1673). As with <simd>, an installed but declaration-free header
# is not partial native support at the configured language standard.
check_cxx_source_compiles("
    #include <linalg>
    #include <mdspan>
    #if !defined(__cpp_lib_linalg) || __cpp_lib_linalg < 202511L
    #error incomplete linalg surface
    #endif
    int main() {
        double vector_data[2] = {3, 4};
        double matrix_data[4] = {1, 2, 3, 4};
        double output_data[4]{};
        std::mdspan<double, std::extents<int, 2>> v(vector_data);
        std::mdspan<double, std::extents<int, 2, 2>> matrix(matrix_data);
        std::mdspan<double, std::extents<int, 2, 2>> output(output_data);
        auto g = std::linalg::setup_givens_rotation(3.0, 4.0);
        std::linalg::matrix_vector_product(matrix, v, v);
        std::linalg::matrix_product(matrix, matrix, output);
        using packed_layout = std::linalg::layout_blas_packed<
            std::linalg::upper_triangle_t,
            std::linalg::column_major_t>;
        packed_layout::mapping<std::extents<int, 2, 2>> packed(
            std::extents<int, 2, 2>{});
        return static_cast<int>(
            std::linalg::vector_two_norm(v) + g.r + packed.required_span_size());
    }
" FORGE_LINALG_FULL)
check_cxx_source_compiles("
    #include <linalg>
    #ifndef __cpp_lib_linalg
    #error no linalg feature macro
    #endif
    int main() { return 0; }
" FORGE_LINALG_PARTIAL_MACRO)
check_cxx_source_compiles("
    #include <linalg>
    using probe = std::linalg::setup_givens_rotation_result<double>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_LINALG_PARTIAL_SETUP)
check_cxx_source_compiles("
    #include <linalg>
    using probe = std::linalg::upper_triangle_t;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_LINALG_PARTIAL_TAG)
check_cxx_source_compiles("
    #include <linalg>
    template<template<class, class> class>
    struct accepts_layout_template {};
    using probe = accepts_layout_template<std::linalg::layout_blas_packed>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_LINALG_PARTIAL_LAYOUT)
check_cxx_source_compiles("
    #include <linalg>
    #include <mdspan>
    int main() {
        double data[1]{};
        std::mdspan<double, std::extents<int, 1, 1>> matrix(data);
        std::linalg::matrix_product(matrix, matrix, matrix);
        return 0;
    }
" FORGE_LINALG_PARTIAL_MATRIX_PRODUCT)
set(FORGE_LINALG_PARTIAL FALSE)
if(FORGE_LINALG_PARTIAL_MACRO
        OR FORGE_LINALG_PARTIAL_SETUP
        OR FORGE_LINALG_PARTIAL_TAG
        OR FORGE_LINALG_PARTIAL_LAYOUT
        OR FORGE_LINALG_PARTIAL_MATRIX_PRODUCT)
    set(FORGE_LINALG_PARTIAL TRUE)
endif()
_forge_decide("std::linalg" LINALG FORGE_LINALG_FULL FORGE_LINALG_PARTIAL)
else()
    target_compile_definitions(${FORGE_BACKPORT_TARGET} INTERFACE
        FORGE_NO_BASE_MDSPAN=1)
    message(STATUS "CC Forge probe: MDSPAN_BASE=UNAVAILABLE")
    message(STATUS "CC Forge probe: MDSPAN_PADDED_LAYOUTS=UNAVAILABLE")
    message(STATUS "CC Forge probe: SUBMDSPAN=UNAVAILABLE")
    message(STATUS "CC Forge probe: LINALG=UNAVAILABLE")
    message(WARNING "CC Forge: the configured standard library has no usable base <mdspan>; padded layouts, submdspan, and linalg backports are unavailable")
endif()

set(CMAKE_REQUIRED_FLAGS "${_forge_saved_required_flags}")
