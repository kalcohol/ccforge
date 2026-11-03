#include <mdspan>

void check_negative_constant_index() {
    const std::dextents<unsigned, 1> source(4);
#ifdef FORGE_SUBMDSPAN_POSITIVE_CONTROL
    (void)std::canonical_slices(source, std::cw<0>);
#else
    (void)std::canonical_slices(source, std::cw<-1>);
#endif
}

int main() { return 0; }
