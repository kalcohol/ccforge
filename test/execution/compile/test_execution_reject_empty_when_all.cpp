#include <execution>

int main() {
    auto sender = std::execution::when_all();
    (void)sender;
}
