#include <mdspan>

using extents_t = std::extents<int, 2, 3>;
static_assert(extents_t::rank() == 2);

int main() {
    int data[6]{};
    std::mdspan<int, extents_t> source(data);
    return source.extent(0) == 2 && source.extent(1) == 3 ? 0 : 1;
}
