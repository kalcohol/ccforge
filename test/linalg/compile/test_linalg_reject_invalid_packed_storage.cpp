#include <linalg>

using invalid_layout = std::linalg::layout_blas_packed<
    std::linalg::upper_triangle_t,
    int>;

invalid_layout layout;

int main() { (void)layout; }
