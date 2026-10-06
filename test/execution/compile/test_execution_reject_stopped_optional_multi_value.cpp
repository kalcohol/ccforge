#include "stopped_optional_probe.hpp"

using namespace stopped_optional_probe;

int main() {
    using source = signature_sender<ex::completion_signatures<
        ex::set_value_t(int), ex::set_value_t(const int&)>>;
    auto sender = source{} | ex::stopped_as_optional;
    (void)ex::get_completion_signatures(std::move(sender), ex::empty_env{});
}
