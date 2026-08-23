#include <linalg>

#include <mdspan>

using invalid_mapping = std::linalg::layout_blas_packed<
    std::linalg::upper_triangle_t,
    std::linalg::row_major_t>::mapping<std::extents<int, 2, 3>>;

invalid_mapping mapping;

int main() { (void)mapping; }
