#include <forge/linalg.hpp>
#include <linalg>
#include <mdspan>

int main() {
    double data[]{3.0, 4.0};
    std::mdspan v(data, std::extents<int, 2>{});
    using seed = forge::sum_of_squares_result<double>;
    (void)std::linalg::vector_two_norm(v);
    (void)forge::vector_sum_of_squares(v, seed{0.0, 1.0});

#if defined(FORGE_TEST_LINALG_SSQ_FUNCTION)
    using std::linalg::vector_sum_of_squares;
#elif defined(FORGE_TEST_LINALG_SSQ_TYPE)
    using std::linalg::sum_of_squares_result;
#endif
}
