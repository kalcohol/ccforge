#include <simd>

using flags_or_ptr = std::simd::flags<std::simd::convert_flag> (*)(
    std::simd::flags<>,
    std::simd::flags<std::simd::convert_flag>) noexcept;

flags_or_ptr ptr = static_cast<flags_or_ptr>(&std::simd::operator|);

int main() {
    return 0;
}
