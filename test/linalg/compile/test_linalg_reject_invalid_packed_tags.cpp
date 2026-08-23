#include <linalg>

using invalid_layout = std::linalg::layout_blas_packed<
    int,
    std::linalg::row_major_t>;

invalid_layout layout;

int main() { (void)layout; }
