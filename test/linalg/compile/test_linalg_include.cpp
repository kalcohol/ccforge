// Compile probe: transparent <linalg> include and representative public names.

#include <linalg>
#include <mdspan>
#include <type_traits>

static_assert(std::is_same_v<decltype(std::linalg::upper_triangle),
                             const std::linalg::upper_triangle_t>);

static void check_include_surface()
{
    double data[] = {3.0, 4.0};
    std::mdspan v(data, std::extents<int, 2>{});
    auto rotation = std::linalg::setup_givens_rotation(3.0, 4.0);
    (void)std::linalg::vector_two_norm(v);
    std::linalg::apply_givens_rotation(v, v, rotation.c, rotation.s);
}

int main()
{
    check_include_surface();
}
