#include <forge/execution_extensions.hpp>
#include <execution>

#include <string>
#include <utility>

int main() {
    auto sender = std::execution::just_error(42)
                | std::execution::upon_error([](std::string) {});
    (void)std::execution::get_completion_signatures(
        sender, forge::empty_env{});
}
