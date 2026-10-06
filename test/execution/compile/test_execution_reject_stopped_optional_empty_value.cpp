#include <execution>
#include <utility>

int main() {
    auto sender = std::execution::just() | std::execution::stopped_as_optional();
    (void)std::execution::get_completion_signatures(
        std::move(sender), std::execution::empty_env{});
}
