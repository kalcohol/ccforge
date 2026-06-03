#include <execution>
#include <memory>

int main() {
    auto sndr = std::execution::just(std::make_unique<int>(1));
    auto adapted = std::execution::then(sndr, [](std::unique_ptr<int>) noexcept {});
    (void)adapted;
}
