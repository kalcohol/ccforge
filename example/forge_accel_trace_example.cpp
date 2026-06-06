#include <forge/accel.hpp>
#include <execution>
#include <cassert>
#include "example_support.hpp"
#include <iostream>
#include <vector>

int main() {
    forge::accel::mock::trace_sink trace;
    auto options = forge::accel::mock::context_options{};
    options.trace = &trace;

    forge::accel::mock::context ctx{options};
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);

    int value = 0;
    forge_example::require(std::execution::sync_wait(
        forge::accel::mock::submit(q, [&] {
            value = 42;
        })).has_value());

    auto events = trace.snapshot();
    forge_example::require(events.size() == 3);
    forge_example::require(events[0].kind == forge::accel::mock::trace_event_kind::submitted);
    forge_example::require(events[1].kind == forge::accel::mock::trace_event_kind::started);
    forge_example::require(events[2].kind == forge::accel::mock::trace_event_kind::completed);
    forge_example::require(events[0].context.value != 0);
    forge_example::require(events[0].stream.value != 0);
    forge_example::require(value == 42);

    std::cout << "command timeline events: " << events.size() << "\n";
    return 0;
}
