#include <simd>

#include <array>
#include <complex>
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using int2 = std::simd::vec<int, 2>;
using int4 = std::simd::vec<int, 4>;
using int5 = std::simd::vec<int, 5>;
using int8 = std::simd::vec<int, 8>;
using int32 = std::simd::vec<int, 32>;
using int64 = std::simd::vec<int, 64>;
using float2 = std::simd::vec<float, 2>;
using float4 = std::simd::vec<float, 4>;
using complex2 = std::simd::vec<std::complex<float>, 2>;
using complex4 = std::simd::vec<std::complex<float>, 4>;
using mask2 = typename int2::mask_type;
using mask4 = typename int4::mask_type;
using mask5 = typename int5::mask_type;
using mask32 = typename int32::mask_type;
using mask64 = typename int64::mask_type;
using byte_mask2 = typename std::simd::vec<unsigned char, 2>::mask_type;
using native_int = std::simd::basic_vec<int>;
using native_mask = typename native_int::mask_type;

struct lookalike {
    using value_type = int;
    static constexpr std::simd::simd_size_type size = 4;
    constexpr int operator[](std::simd::simd_size_type i) const noexcept {
        return static_cast<int>(i);
    }
};

struct unrelated {};

struct derived_int4 : int4 {
    using int4::int4;
};

struct derived_mask4 : mask4 {
    using mask4::mask4;
};

template<class Chunk, class V>
concept can_chunk_type = requires(const V& value) {
    std::simd::chunk<Chunk>(value);
};

template<std::simd::simd_size_type N, class V>
concept can_chunk_width = requires(const V& value) {
    std::simd::chunk<N>(value);
};

template<class... Vs>
concept can_cat = requires(const Vs&... values) {
    std::simd::cat(values...);
};

template<class Chunk, class V>
concept nothrow_chunk_type = requires(const V& value) {
    { std::simd::chunk<Chunk>(value) } noexcept;
};

template<std::simd::simd_size_type N, class V>
concept nothrow_chunk_width = requires(const V& value) {
    { std::simd::chunk<N>(value) } noexcept;
};

template<class... Vs>
concept nothrow_cat = requires(const Vs&... values) {
    { std::simd::cat(values...) } noexcept;
};

static_assert(can_chunk_type<int2, int4>);
static_assert(can_chunk_type<int2, const int4>);
static_assert(can_chunk_type<int2, int5>);
static_assert(can_chunk_type<int8, int4>);
static_assert(can_chunk_type<complex2, complex4>);
static_assert(can_chunk_type<mask2, mask4>);
static_assert(can_chunk_type<mask2, const mask4>);
static_assert(can_chunk_type<mask2, mask5>);
static_assert(can_chunk_type<mask2, native_mask>);
static_assert(can_chunk_type<int2, derived_int4>);
static_assert(can_chunk_type<mask2, derived_mask4>);
static_assert(can_chunk_width<2, int4>);
static_assert(can_chunk_width<2, mask4>);
static_assert(can_chunk_width<2, derived_int4>);
static_assert(can_chunk_width<2, derived_mask4>);
static_assert(can_chunk_width<3, int5>);
static_assert(can_chunk_width<3, mask5>);
static_assert(can_chunk_width<64, int4>);
static_assert(can_chunk_width<64, mask4>);

static_assert(!can_chunk_type<float2, int4>);
static_assert(!can_chunk_type<int2, float4>);
static_assert(!can_chunk_type<mask2, int4>);
static_assert(!can_chunk_type<int2, mask4>);
static_assert(!can_chunk_type<byte_mask2, mask4>);
static_assert(!can_chunk_type<mask2, byte_mask2>);
static_assert(!can_chunk_type<lookalike, int4>);
static_assert(!can_chunk_type<unrelated, int4>);
static_assert(!can_chunk_type<int2, lookalike>);
static_assert(!can_chunk_type<int2, unrelated>);
static_assert(!can_chunk_type<const int2, int4>);
static_assert(!can_chunk_type<int2&, int4>);
static_assert(!can_chunk_type<const mask2, mask4>);
static_assert(!can_chunk_type<mask2&, mask4>);
static_assert(!can_chunk_width<2, lookalike>);
static_assert(!can_chunk_width<2, unrelated>);
static_assert(!can_chunk_width<0, int4>);
static_assert(!can_chunk_width<-1, int4>);
static_assert(!can_chunk_width<65, int4>);
static_assert(!can_chunk_width<0, mask4>);
static_assert(!can_chunk_width<-1, mask4>);
static_assert(!can_chunk_width<65, mask4>);

static_assert(can_cat<int2>);
static_assert(can_cat<int2, int4>);
static_assert(can_cat<const int2, const int4>);
static_assert(can_cat<int2, native_int>);
static_assert(can_cat<complex2, complex4>);
static_assert(can_cat<mask2, mask4>);
static_assert(can_cat<mask2, native_mask>);
static_assert(can_cat<derived_int4, int2>);
static_assert(can_cat<derived_mask4, mask2>);
static_assert(can_cat<int32, int32>);
static_assert(can_cat<mask32, mask32>);
static_assert(!can_cat<>);
static_assert(!can_cat<int2, float2>);
static_assert(!can_cat<int2, mask2>);
static_assert(!can_cat<mask2, int2>);
static_assert(!can_cat<mask2, byte_mask2>);
static_assert(!can_cat<int2, lookalike>);
static_assert(!can_cat<lookalike, int2>);
static_assert(!can_cat<lookalike, lookalike>);
static_assert(!can_cat<unrelated>);
static_assert(!can_cat<int64, int2>);
static_assert(!can_cat<int32, int32, int2>);
static_assert(!can_cat<mask64, mask2>);
static_assert(!can_cat<mask32, mask32, mask2>);

static_assert(nothrow_chunk_type<int2, int4>);
static_assert(nothrow_chunk_type<int2, int5>);
static_assert(nothrow_chunk_type<int8, int4>);
static_assert(nothrow_chunk_type<complex2, complex4>);
static_assert(nothrow_chunk_type<mask2, mask4>);
static_assert(nothrow_chunk_type<mask2, mask5>);
static_assert(nothrow_chunk_type<int2, derived_int4>);
static_assert(nothrow_chunk_type<mask2, derived_mask4>);
static_assert(nothrow_chunk_width<2, int4>);
static_assert(nothrow_chunk_width<3, int5>);
static_assert(nothrow_chunk_width<2, mask4>);
static_assert(nothrow_chunk_width<3, mask5>);
static_assert(nothrow_cat<int2>);
static_assert(nothrow_cat<int2, int4>);
static_assert(nothrow_cat<int2, native_int>);
static_assert(nothrow_cat<complex2, complex4>);
static_assert(nothrow_cat<mask2, mask4>);
static_assert(nothrow_cat<mask2, native_mask>);
static_assert(nothrow_cat<derived_int4, int2>);
static_assert(nothrow_cat<derived_mask4, mask2>);

using explicit_vector_chunks = decltype(std::simd::chunk<int2, typename int4::abi_type>(
    std::declval<const int4&>()));
using explicit_mask_chunks = decltype(std::simd::chunk<mask2, typename mask4::abi_type>(
    std::declval<const mask4&>()));
using explicit_vector_width = decltype(std::simd::chunk<2, int, typename int4::abi_type>(
    std::declval<const int4&>()));
using explicit_mask_width = decltype(std::simd::chunk<2, sizeof(int), typename mask4::abi_type>(
    std::declval<const mask4&>()));
using explicit_vector_cat = decltype(std::simd::cat<int>(
    std::declval<const int2&>(), std::declval<const int4&>()));
using explicit_mask_cat = decltype(std::simd::cat<sizeof(int)>(
    std::declval<const mask2&>(), std::declval<const mask4&>()));
static_assert(std::same_as<explicit_vector_chunks, std::array<int2, 2>>);
static_assert(std::same_as<explicit_mask_chunks, std::array<mask2, 2>>);
static_assert(std::same_as<explicit_vector_width, explicit_vector_chunks>);
static_assert(std::same_as<explicit_mask_width, explicit_mask_chunks>);
static_assert(std::same_as<explicit_vector_cat, std::simd::resize_t<6, int2>>);
static_assert(std::same_as<explicit_mask_cat, std::simd::resize_t<6, mask2>>);

using even_chunks = decltype(std::simd::chunk<int2>(std::declval<const int4&>()));
using uneven_chunks = decltype(std::simd::chunk<int2>(std::declval<const int5&>()));
using larger_chunks = decltype(std::simd::chunk<int8>(std::declval<const int4&>()));
using uneven_masks = decltype(std::simd::chunk<mask2>(std::declval<const mask5&>()));
static_assert(std::same_as<even_chunks, std::array<int2, 2>>);
static_assert(std::same_as<uneven_chunks,
    std::tuple<int2, int2, std::simd::resize_t<1, int2>>>);
static_assert(std::same_as<larger_chunks, std::tuple<std::simd::resize_t<4, int8>>>);
static_assert(std::same_as<uneven_masks,
    std::tuple<mask2, mask2, std::simd::resize_t<1, mask2>>>);
static_assert(std::same_as<decltype(std::simd::cat(
    std::declval<const int2&>(), std::declval<const int4&>())),
    std::simd::resize_t<6, int2>>);
static_assert(std::same_as<decltype(std::simd::cat(
    std::declval<const mask2&>(), std::declval<const mask4&>())),
    std::simd::resize_t<6, mask2>>);

} // namespace

int main() {}
