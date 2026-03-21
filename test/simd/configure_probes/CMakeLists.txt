forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_WHERE_COMPOUND_OPS
    "where_compound_ops.cpp"
    TRUE
    "Forge simd backport must support where(mask, vec) compound assignment operators")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_WHERE_INT_SCALAR_OPS
    "where_int_scalar_ops.cpp"
    TRUE
    "Forge simd backport must support integral where(mask, vec) scalar compound operators")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_WHERE_BOOL
    "where_bool.cpp"
    TRUE
    "Forge simd backport must support where(bool, vec)")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_WHERE_MASK
    "where_mask.cpp"
    TRUE
    "Forge simd backport must support where(mask, mask)")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_MASK_CAST
    "mask_cast.cpp"
    TRUE
    "Forge simd backport must support mask simd_cast/static_simd_cast")
forge_add_simd_configure_probe(
    FORGE_SIMD_ACCEPTS_NON_VALUE_PRESERVING_LOAD_STORE
    "reject_non_value_preserving_load_store.cpp"
    FALSE
    "Forge simd backport must reject non-value-preserving loads/stores without std::simd::flag_convert")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_LONG_DOUBLE_MASK
    "long_double_mask_unary.cpp"
    TRUE
    "Forge simd backport must support long double mask unary operators")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_NATIVE_MASK_UNARY
    "native_mask_unary.cpp"
    TRUE
    "Forge simd backport must support native-width mask unary operators across float, double, and unsigned int")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_IMPLICIT_VEC_CONVERTER
    "reject_implicit_vec_converter.cpp"
    FALSE
    "Forge simd backport must reject vector converting constructors without std::simd::flag_convert")
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
    FORGE_SIMD_HAS_VALUE_PRESERVING_VEC_CONVERTER
    "value_preserving_vec_converter.cpp"
    TRUE
    "Forge simd backport must support value-preserving vector converting constructors")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_IMPLICIT_SCALAR_NARROWING_BROADCAST
    "reject_implicit_scalar_narrowing_broadcast.cpp"
    FALSE
    "Forge simd backport must reject non-value-preserving implicit scalar broadcasts")
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
    FORGE_SIMD_HAS_DEFAULT_SUPPORTED_LOAD_STORE
    "default_supported_load_store.cpp"
    TRUE
    "Forge simd backport must infer supported complex and extended-integer default load vectors for pointer/count memory entry points")
forge_add_simd_configure_probe(
    FORGE_SIMD_ACCEPTS_DEQUE_ITERATOR
    "reject_deque_iterator.cpp"
    FALSE
    "Forge simd backport must reject non-contiguous iterators for memory operations")
forge_add_simd_configure_probe(
    FORGE_SIMD_HAS_RANGE_GATHER_SCATTER
    "range_gather_scatter.cpp"
    TRUE
    "Forge simd backport must support contiguous range gather/scatter overloads")
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
