#include <simd>

#include <span>
#include <type_traits>

int main() {
    using default_int = std::simd::basic_vec<int>;
    using default_mask = typename default_int::mask_type;

    static_assert(std::is_same_v<decltype(std::simd::partial_load(std::declval<std::span<const int, 4>>(), default_mask{})),
                                 default_int>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_load(std::declval<std::span<const int, 4>>(),
                                                                    default_mask{},
                                                                    std::simd::flag_default)),
                                 default_int>);
    static_assert(std::is_same_v<decltype(std::simd::partial_store(std::declval<const default_int&>(),
                                                                   std::declval<std::span<int, 4>>(),
                                                                   default_mask{})),
                                 void>);
    static_assert(std::is_same_v<decltype(std::simd::unchecked_store(std::declval<const default_int&>(),
                                                                     std::declval<std::span<int, 4>>(),
                                                                     default_mask{},
                                                                     std::simd::flag_default)),
                                 void>);

    return 0;
}
