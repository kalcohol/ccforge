#include <utility>

using invalid_wrapper = std::constant_wrapper<42, unsigned>;

int main() {
    return sizeof(invalid_wrapper);
}
