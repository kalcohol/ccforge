#include <linalg>
#include <mdspan>

int main() {
    double data[]{3.0, 4.0};
    std::mdspan v(data, std::extents<int, 2>{});
    (void)std::linalg::vector_two_norm(v);

#if defined(FORGE_TEST_LINALG_SSQ_FUNCTION)
    using std::linalg::vector_sum_of_squares;
#elif defined(FORGE_TEST_LINALG_SSQ_TYPE)
    using std::linalg::sum_of_squares_result;
#endif
}
