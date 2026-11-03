#include <mdspan>

int main() {
    using extents_t = std::extents<int, 3, 8>;
#ifdef FORGE_SUBMDSPAN_POSITIVE_CONTROL
    const std::layout_right_padded<8>::mapping<extents_t> source(extents_t{});
#else
    const std::layout_right_padded<4>::mapping<extents_t> source(extents_t{});
#endif
    // Equal effective strides do not make unequal static padding values compatible.
    const std::layout_right_padded<8>::mapping<extents_t> target(source);
    return target(2, 7);
}
