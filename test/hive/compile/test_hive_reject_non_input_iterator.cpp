#include <hive>

struct not_an_iterator {};

int main() {
    std::hive hive(not_an_iterator{}, not_an_iterator{});
}
