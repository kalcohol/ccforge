#include <mdspan>

int main() {
    using extents_t = std::extents<int, 5, 3>;
    const std::layout_right_padded<8>::mapping<extents_t> source(extents_t{});
    const std::layout_right::mapping<extents_t> target = source;
    return target(4, 2);
}
