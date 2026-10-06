#include <mdspan>

int main() {
    using extents_t = std::extents<int, 3, 5>;
    const std::layout_left::mapping<extents_t> source(extents_t{});
    const std::layout_left_padded<8>::mapping<extents_t> target(source);
    return target(2, 4);
}
