#include <simd>

using flags_or_ptr = std::simd::flags<std::simd::convert_flag> (*)(
    std::simd::flags<>,
    std::simd::flags<std::simd::convert_flag>) noexcept;

#if defined(FORGE_SIMD_POSITIVE_CONTROL)
constexpr auto combined = std::simd::flag_default | std::simd::flag_convert;
#else
flags_or_ptr ptr = static_cast<flags_or_ptr>(&std::simd::operator|);
#endif

int main() {
    return 0;
}
