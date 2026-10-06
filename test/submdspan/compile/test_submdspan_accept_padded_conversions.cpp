#include <mdspan>

int main() {
    using left_extents_t = std::extents<int, 8, 3>;
    using right_extents_t = std::extents<int, 3, 8>;
    constexpr std::layout_left::mapping<left_extents_t> left(left_extents_t{});
    constexpr std::layout_right::mapping<right_extents_t> right(right_extents_t{});
    constexpr std::layout_left_padded<8>::mapping<left_extents_t> left_padded(left);
    constexpr std::layout_right_padded<8>::mapping<right_extents_t> right_padded(right);
    constexpr std::layout_left::mapping<left_extents_t> left_roundtrip = left_padded;
    constexpr std::layout_right::mapping<right_extents_t> right_roundtrip = right_padded;
    constexpr std::layout_left_padded<8>::mapping<std::extents<long, 8, 3>>
        left_widened(left_padded);
    constexpr std::layout_right_padded<8>::mapping<std::extents<long, 3, 8>>
        right_widened(right_padded);
    static_assert(left_roundtrip(7, 2) == left(7, 2));
    static_assert(right_roundtrip(2, 7) == right(2, 7));
    static_assert(left_widened(7, 2) == 23);
    static_assert(right_widened(2, 7) == 23);
}
