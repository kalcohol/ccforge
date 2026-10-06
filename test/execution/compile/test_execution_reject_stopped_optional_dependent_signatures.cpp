#include "stopped_optional_probe.hpp"

using namespace stopped_optional_probe;

int main() {
    auto sender = ex::stopped_as_optional(dependent_sender{});
    (void)ex::get_completion_signatures(sender, void_env{});
}
