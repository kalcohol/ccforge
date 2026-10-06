#include <execution>
#include <utility>

int main() {
    auto sender = std::execution::stopped_as_optional(std::execution::just_stopped());
    (void)std::execution::get_completion_signatures(
        std::move(sender), std::execution::empty_env{});
}
