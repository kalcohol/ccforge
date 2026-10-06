#include "stopped_optional_probe.hpp"

using namespace stopped_optional_probe;

int main() {
    const auto sender = ex::stopped_as_optional(dependent_sender{});
    auto operation = sender.connect(receiver<multi_env>{});
    (void)operation;
}
