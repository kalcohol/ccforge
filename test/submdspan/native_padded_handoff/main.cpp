#include <mdspan>

using slice_t = std::extent_slice<int, int, int>;

static_assert(sizeof(slice_t) > 0);

int main() {}
