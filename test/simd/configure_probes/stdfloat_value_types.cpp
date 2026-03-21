#include <simd>

#if defined(__has_include)
#  if __has_include(<stdfloat>)
#    include <stdfloat>
#  endif
#endif

int main() {
#ifdef __STDCPP_FLOAT16_T__
    std::simd::vec<std::float16_t, 4> half_value(std::float16_t{1.0f});
    static_assert(decltype(half_value)::size == 4);
#endif
#ifdef __STDCPP_FLOAT32_T__
    std::simd::vec<std::float32_t, 4> single_value(std::float32_t{1.0f});
    static_assert(decltype(single_value)::size == 4);
#endif
#ifdef __STDCPP_FLOAT64_T__
    std::simd::vec<std::float64_t, 4> double_value(std::float64_t{1.0});
    static_assert(decltype(double_value)::size == 4);
#endif
    return 0;
}
