forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_NONSTANDARD_WHERE
    "reject_nonstandard_where.cpp"
    FALSE
    "Forge simd backport must not expose non-standard where APIs")
forge_add_simd_configure_probe(
    FORGE_SIMD_ACCEPTS_NON_VALUE_PRESERVING_LOAD_STORE
    "reject_non_value_preserving_load_store.cpp"
    FALSE
    "Forge simd backport must reject non-value-preserving loads/stores without std::simd::flag_convert")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_NATIVE_MASK_UNARY
    "native_mask_unary.cpp"
    TRUE
    "Forge simd backport must support native-width mask unary operators across float, double, and unsigned int")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_LONG_DOUBLE_VALUE
    "reject_long_double_value.cpp"
    FALSE
    "Forge simd backport must reject non-vectorizable long double SIMD specializations")
unset(FORGE_SIMD_HAS_EXTENDED_INTEGER_TYPE CACHE)
unset(FORGE_SIMD_HAS_EXTENDED_INTEGER_TYPE)
check_cxx_source_compiles(
    "#if !defined(__SIZEOF_INT128__)
     #error __int128 unavailable
     #endif
     int main() { __int128 value = 0; return static_cast<int>(value); }"
    FORGE_SIMD_HAS_EXTENDED_INTEGER_TYPE)
if(FORGE_SIMD_HAS_EXTENDED_INTEGER_TYPE)
    forge_add_simd_configure_probe(
        FORGE_SIMD_REJECTS_EXTENDED_INTEGER
        "reject_extended_integer.cpp"
        FALSE
        "Forge simd backport must reject non-standard extended integer SIMD specializations")
else()
    message(STATUS "Forge: skipping extended-integer SIMD rejection probe; compiler has no __int128")
endif()
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_IMPLICIT_VEC_CONVERTER
    "reject_implicit_vec_converter.cpp"
    FALSE
    "Forge simd backport must keep non-value-preserving vector converting constructors explicit")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_FLAG_CONVERT_CONSTRUCTOR
    "flag_convert_constructor.cpp"
    TRUE
    "Forge simd backport must provide vector converting constructors with std::simd::flag_convert")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_FLAGS_OR_ADDRESS
    "flags_rejects_address.cpp"
    FALSE
    "Forge simd backport must make std::simd::flags operator| an immediate function")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_INVALID_FLAGS
    "reject_invalid_flags.cpp"
    FALSE
    "Forge simd backport must constrain std::simd::flags to standard flag types")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_VALUE_PRESERVING_VEC_CONVERTER
    "value_preserving_vec_converter.cpp"
    TRUE
    "Forge simd backport must support value-preserving vector converting constructors")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_IMPLICIT_SCALAR_NARROWING_BROADCAST
    "reject_implicit_scalar_narrowing_broadcast.cpp"
    FALSE
    "Forge simd backport must reject non-value-preserving scalar broadcasts")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_NARROWING_GENERATOR
    "reject_narrowing_generator.cpp"
    FALSE
    "Forge simd backport must reject non-value-preserving generator construction")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_RANGE_LOAD_STORE
    "range_load_store.cpp"
    TRUE
    "Forge simd backport must support contiguous range constructors and load/store overloads")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_RANGE_MASKED_LOAD_STORE
    "range_masked_load_store.cpp"
    TRUE
    "Forge simd backport must support masked contiguous range load/store overloads")
set(_forge_static_extent_definitions "${CMAKE_REQUIRED_DEFINITIONS}")
foreach(_forge_range RANGE 0 5)
    foreach(_forge_operation RANGE 0 2)
        foreach(_forge_accept RANGE 0 1)
            set(CMAKE_REQUIRED_DEFINITIONS ${_forge_static_extent_definitions}
                -DSIMD14_RANGE=${_forge_range}
                -DSIMD14_OPERATION=${_forge_operation}
                -DSIMD14_MASKED=1
                -DSIMD14_ACCEPT=${_forge_accept})
            forge_add_simd_configure_probe(
                FORGE_SIMD_STATIC_EXTENT_${_forge_range}_${_forge_operation}_${_forge_accept}
                "reject_short_unchecked_range.cpp"
                ${_forge_accept}
                "Unchecked SIMD range loads/stores must enforce static extent Mandates")
        endforeach()
    endforeach()
endforeach()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_static_extent_definitions}")
unset(_forge_static_extent_definitions)
unset(_forge_range)
unset(_forge_operation)
unset(_forge_accept)
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_DEFAULT_SUPPORTED_LOAD_STORE
    "default_supported_load_store.cpp"
    TRUE
    "Forge simd backport must infer supported standard value types for pointer/count memory entry points")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_CROSS_TYPE_ALIGNMENT
    "cross_type_alignment.cpp"
    TRUE
    "Forge simd backport must expose alignment_v for basic_vec across vectorizable scalar types")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_MASK_ALIGNMENT
    "reject_mask_alignment.cpp"
    FALSE
    "Forge simd backport must not expose alignment_v for basic_mask specializations")
forge_add_simd_configure_probe(
    FORGE_SIMD_ACCEPTS_DEQUE_ITERATOR
    "reject_deque_iterator.cpp"
    FALSE
    "Forge simd backport must reject non-contiguous iterators for memory operations")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_OVERSIZED_FIXED_SIZE
    "reject_oversized_fixed_size.cpp"
    FALSE
    "Forge simd backport must reject fixed-size SIMD aliases above the supported maximum")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_RANGE_GATHER_SCATTER
    "range_gather_scatter.cpp"
    TRUE
    "Forge simd backport must support contiguous range gather/scatter overloads")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_RANGE_MASKED_GATHER_SCATTER
    "range_masked_gather_scatter.cpp"
    TRUE
    "Forge simd backport must support masked contiguous range gather/scatter overloads and supported-type default gathers")
forge_add_simd_configure_probe(
    FORGE_SIMD_ACCEPTS_MISMATCHED_PARTIAL_GATHER
    "reject_mismatched_partial_gather.cpp"
    FALSE
    "Forge simd backport must diagnose partial gather vectors whose widths differ from their index vectors")
forge_add_simd_configure_probe(
    FORGE_SIMD_ACCEPTS_MISMATCHED_MASKED_PARTIAL_GATHER
    "reject_mismatched_masked_partial_gather.cpp"
    FALSE
    "Forge simd backport must diagnose masked partial gather vectors whose widths differ from their index vectors")
forge_add_simd_configure_probe(
    FORGE_SIMD_ACCEPTS_MISMATCHED_UNCHECKED_GATHER
    "reject_mismatched_unchecked_gather.cpp"
    FALSE
    "Forge simd backport must diagnose unchecked gather vectors whose widths differ from their index vectors")
forge_add_simd_configure_probe(
    FORGE_SIMD_ACCEPTS_MISMATCHED_MASKED_UNCHECKED_GATHER
    "reject_mismatched_masked_unchecked_gather.cpp"
    FALSE
    "Forge simd backport must diagnose masked unchecked gather vectors whose widths differ from their index vectors")
forge_add_simd_configure_probe(
    FORGE_SIMD_ACCEPTS_LEGACY_MASKED_GATHER_SCATTER_ORDER
    "reject_legacy_masked_gather_scatter_order.cpp"
    FALSE
    "Forge simd backport must reject the legacy masked gather/scatter parameter order")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_COMPRESS_EXPAND_OVERLOADS
    "compress_expand_overloads.cpp"
    TRUE
    "Forge simd backport must provide the repaired compress/expand/select overload set")
forge_add_simd_configure_probe(
    FORGE_SIMD_ACCEPTS_STATEFUL_STATIC_PERMUTE
    "reject_stateful_static_permute.cpp"
    FALSE
    "Forge simd backport must diagnose static permute maps whose results are not constant expressions")
set(_forge_permute_meta_definitions "${CMAKE_REQUIRED_DEFINITIONS}")
foreach(_forge_permute_meta_reject RANGE 1 8)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_permute_meta_definitions}
        -DFORGE_SIMD_STATIC_PERMUTE_META_REJECT=${_forge_permute_meta_reject})
    forge_add_simd_configure_probe(
        FORGE_SIMD_REJECTS_STATIC_PERMUTE_META_${_forge_permute_meta_reject}
        "../compile/test_simd_static_permute_meta_gcc.cpp"
        FALSE
        "Static SIMD permutation metadata must reject nonconstant or invalid maps")
endforeach()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_permute_meta_definitions}")
unset(_forge_permute_meta_definitions)
unset(_forge_permute_meta_reject)
forge_add_simd_configure_probe(
    FORGE_SIMD_ACCEPTS_OUT_OF_RANGE_STATIC_PERMUTE
    "reject_out_of_range_static_permute.cpp"
    FALSE
    "Forge simd backport must diagnose static permute maps that produce invalid source indices")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_NEGATIVE_FRACTIONAL_STATIC_PERMUTE
    "reject_negative_fractional_static_permute.cpp"
    FALSE
    "Forge simd backport must validate static permute indices before conversion")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_BIT_SURFACE
    "bit_surface.cpp"
    TRUE
    "Forge simd backport must provide the required bit algorithm surface and return types")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_SIGNED_BIT
    "bit_rejects_signed.cpp"
    FALSE
    "Forge simd backport must reject unsigned-only bit algorithms on signed vectors")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_CONSTEXPR_NONPOSITIVE_BIT_REPEAT
    "bit_rejects_constexpr_nonpositive_repeat.cpp"
    FALSE
    "Forge simd backport must reject non-positive bit_repeat lengths in constant expressions")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_LEGACY_SPLIT
    "reject_legacy_split.cpp"
    FALSE
    "Forge simd backport must not expose the removed parallelism-TS split extension")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_LEGACY_SIMD_CAST
    "reject_legacy_simd_cast.cpp"
    FALSE
    "Forge simd backport must not expose the removed parallelism-TS simd_cast extension")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_MATH_SURFACE
    "math_surface.cpp"
    TRUE
    "Forge simd backport must provide the required math algorithm surface")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_MATH_SPECIAL_SURFACE
    "math_special_surface.cpp"
    TRUE
    "Forge simd backport must provide the required special math surface")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_INTEGRAL_MATH
    "math_rejects_integral.cpp"
    FALSE
    "Forge simd backport must reject floating-only math algorithms on integral vectors")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_PAIR_DECOMPOSITION
    "math_rejects_pair_decompose.cpp"
    FALSE
    "Forge simd backport must not expose obsolete pair-return decomposition signatures")
forge_add_simd_configure_probe(
    FORGE_SIMD_REJECTS_CONSTEXPR_INT_ROUND
    "math_rejects_constexpr_int_round.cpp"
    FALSE
    "Forge simd backport must not expose lrint/llrint as constexpr")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_COMPLEX_SURFACE
    "complex_surface.cpp"
    TRUE
    "Forge simd backport must provide the required complex simd surface")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_STDFLOAT_VALUE_TYPES
    "stdfloat_value_types.cpp"
    TRUE
    "Forge simd backport must support current-draft stdfloat vectorizable types when the implementation defines them")

# Same-source controls for rejection fixtures.
set(_forge_pair_definitions "${CMAKE_REQUIRED_DEFINITIONS}")
list(APPEND CMAKE_REQUIRED_DEFINITIONS -DFORGE_SIMD_POSITIVE_CONTROL=1)
foreach(_forge_pair IN ITEMS
        long_double_value
        invalid_flags
        implicit_scalar_narrowing_broadcast
        implicit_vec_converter
        narrowing_generator
        deque_iterator
        oversized_fixed_size
        out_of_range_static_permute
        stateful_static_permute
        negative_fractional_static_permute
        mask_alignment
        mismatched_partial_gather
        mismatched_masked_partial_gather
        mismatched_unchecked_gather
        mismatched_masked_unchecked_gather)
    forge_add_simd_configure_probe(
        FORGE_SIMD_POSITIVE_CONTROL_${_forge_pair}
        "reject_${_forge_pair}.cpp"
        TRUE
        "SIMD negative probe ${_forge_pair} must have a compiling same-source control")
endforeach()
# Removed APIs are controlled by supported alternatives, not the same overload.
foreach(_forge_pair IN ITEMS nonstandard_where legacy_split legacy_simd_cast)
    forge_add_simd_configure_probe(
        FORGE_SIMD_POSITIVE_CONTROL_${_forge_pair}
        "reject_${_forge_pair}.cpp" TRUE
        "Removed SIMD API ${_forge_pair} must have a compiling supported-API control")
endforeach()
forge_add_simd_configure_probe(
    FORGE_SIMD_POSITIVE_CONTROL_flags_rejects_address
    "flags_rejects_address.cpp" TRUE
    "Immediate flags operator must accept evaluation; this is not an address-taking twin")
if(FORGE_SIMD_HAS_EXTENDED_INTEGER_TYPE)
    forge_add_simd_configure_probe(
        FORGE_SIMD_POSITIVE_CONTROL_extended_integer
        "reject_extended_integer.cpp" TRUE
        "Extended integer rejection must have a compiling standard integer control")
endif()
foreach(_forge_pair IN ITEMS bit_rejects_signed bit_rejects_constexpr_nonpositive_repeat math_rejects_integral)
    forge_add_simd_configure_probe(
        FORGE_SIMD_POSITIVE_CONTROL_${_forge_pair}
        "${_forge_pair}.cpp" TRUE
        "SIMD negative probe ${_forge_pair} must have a compiling boundary control")
endforeach()
foreach(_forge_permute_meta_reject RANGE 1 8)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_pair_definitions}
        -DFORGE_SIMD_POSITIVE_CONTROL=1
        -DFORGE_SIMD_STATIC_PERMUTE_META_REJECT=${_forge_permute_meta_reject})
    forge_add_simd_configure_probe(
        FORGE_SIMD_STATIC_PERMUTE_META_CONTROL_${_forge_permute_meta_reject}
        "../compile/test_simd_static_permute_meta_gcc.cpp" TRUE
        "Static permutation negative map ${_forge_permute_meta_reject} must have a valid map control")
endforeach()
# Isolate operations so an earlier rejection cannot hide a later one.
foreach(_forge_pair IN ITEMS
        reject_non_value_preserving_load_store
        reject_legacy_masked_gather_scatter_order
        math_rejects_pair_decompose
        math_rejects_constexpr_int_round)
    set(_forge_last_operation 1)
    if(_forge_pair STREQUAL "reject_legacy_masked_gather_scatter_order")
        set(_forge_last_operation 3)
    elseif(_forge_pair STREQUAL "math_rejects_pair_decompose")
        set(_forge_last_operation 2)
    endif()
    foreach(_forge_operation RANGE 0 ${_forge_last_operation})
        foreach(_forge_accept RANGE 0 1)
            set(CMAKE_REQUIRED_DEFINITIONS ${_forge_pair_definitions}
                -DFORGE_SIMD_PROBE_OPERATION=${_forge_operation})
            if(_forge_accept)
                list(APPEND CMAKE_REQUIRED_DEFINITIONS -DFORGE_SIMD_POSITIVE_CONTROL=1)
            endif()
            forge_add_simd_configure_probe(
                FORGE_SIMD_OPERATION_CONTROL_${_forge_pair}_${_forge_operation}_${_forge_accept}
                "${_forge_pair}.cpp" ${_forge_accept}
                "SIMD ${_forge_pair} operation ${_forge_operation} must have an isolated rejection/control pair")
        endforeach()
    endforeach()
endforeach()
# The original mask/bool rejection has two invalid template arguments.
# Both isolated negatives share the vector/int positive control above.
foreach(_forge_alignment_case RANGE 0 1)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_pair_definitions}
        -DFORGE_SIMD_MASK_ALIGNMENT_CASE=${_forge_alignment_case})
    forge_add_simd_configure_probe(
        FORGE_SIMD_REJECTS_ALIGNMENT_CONDITION_${_forge_alignment_case}
        "reject_mask_alignment.cpp" FALSE
        "SIMD alignment must independently reject masks and unsupported bool scalar types")
endforeach()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_pair_definitions}")
unset(_forge_pair_definitions)
unset(_forge_pair)
unset(_forge_permute_meta_reject)
unset(_forge_last_operation)
unset(_forge_operation)
unset(_forge_accept)
unset(_forge_alignment_case)

# Check the same source both positively and negatively so a broken include or
# assertion cannot masquerade as a successful deprecation diagnostic probe.
set(_forge_simd_deprecation_flags "${CMAKE_REQUIRED_FLAGS}")
set(_forge_simd_deprecation_defs "${CMAKE_REQUIRED_DEFINITIONS}")
if(MSVC)
    string(APPEND CMAKE_REQUIRED_FLAGS " /we4996")
else()
    string(APPEND CMAKE_REQUIRED_FLAGS " -Werror=deprecated-declarations")
endif()
forge_add_simd_configure_probe(FORGE_SIMD_COMPAT_ACCEPTED
    "../compile/test_simd_extension_compat.cpp" TRUE
    "SIMD compatibility aliases must remain equivalent with local suppression")
# A member alias in a class template can lose its deprecation diagnostic on
# GCC/Clang. Case 14 stays in the suppressed positive control, but is not a
# portable expected failure. Keep the list explicit so case 15 still runs.
foreach(_forge_simd_case IN ITEMS 1 2 3 4 5 6 7 8 9 10 11 12 13 15)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_simd_deprecation_defs}
        -DFORGE_SIMD_DEPRECATION_CASE=${_forge_simd_case})
    forge_add_simd_configure_probe(FORGE_SIMD_DEPRECATED_${_forge_simd_case}
        "../compile/test_simd_extension_compat.cpp" FALSE
        "Each old SIMD spelling must diagnose deprecation without suppression")
endforeach()
set(CMAKE_REQUIRED_FLAGS "${_forge_simd_deprecation_flags}")
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_simd_deprecation_defs}")
unset(_forge_simd_deprecation_flags)
unset(_forge_simd_case)

# Former class-template names now follow alias-template specialization rules.
foreach(_forge_simd_alias_case RANGE 0 2)
    set(CMAKE_REQUIRED_DEFINITIONS ${_forge_simd_deprecation_defs}
        -DFORGE_SIMD_ALIAS_SPECIALIZATION_CASE=${_forge_simd_alias_case})
    if(_forge_simd_alias_case EQUAL 0)
        set(_forge_simd_alias_accept TRUE)
    else()
        set(_forge_simd_alias_accept FALSE)
    endif()
    forge_add_simd_configure_probe(FORGE_SIMD_ALIAS_SPECIALIZATION_${_forge_simd_alias_case}
        "reject_legacy_alias_specialization.cpp" ${_forge_simd_alias_accept}
        "Legacy SIMD aliases must support template arguments but reject explicit and partial specialization")
endforeach()
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_simd_deprecation_defs}")
unset(_forge_simd_deprecation_defs)
unset(_forge_simd_alias_case)
unset(_forge_simd_alias_accept)
