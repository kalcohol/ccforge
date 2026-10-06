#include <mdspan>

void check_negative_constant_index() {
    const std::dextents<unsigned, 1> source(4);
    (void)std::canonical_slices(source, std::cw<-1>);
}

int main() { return 0; }
