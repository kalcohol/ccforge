#include "stopped_optional_probe.hpp"

using namespace stopped_optional_probe;

int main() {
    auto sender = ex::stopped_as_optional(dependent_sender{});
    auto operation = std::move(sender).connect(receiver<no_value_env>{});
    (void)operation;
}
