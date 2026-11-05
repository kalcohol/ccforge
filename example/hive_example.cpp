#include <hive>
#include <iostream>

int main() {
    std::hive<int> values;
    values.insert(10);
    auto removed = values.insert(20);
    auto retained = values.insert(30);
    values.erase(removed);
    values.insert(40);

    if (*retained != 30 || values.size() != 3) {
        return 1;
    }
    for (int value : values) {
        std::cout << value << '\n';
    }
}
