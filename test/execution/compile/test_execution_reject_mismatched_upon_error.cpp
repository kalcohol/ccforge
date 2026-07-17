#include <execution>

#include <string>
#include <utility>

int main() {
    auto sender = std::execution::just_error(42)
                | std::execution::upon_error([](std::string) {});
    (void)std::execution::get_completion_signatures(
        sender, std::execution::empty_env{});
}
