#include <forge/accel.hpp>
#include <execution>
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    forge::accel::mock::trace_sink trace;
    auto options = forge::accel::mock::context_options{};
    options.trace = &trace;

    forge::accel::mock::context ctx{options};
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);

    int value = 0;
    assert(std::execution::sync_wait(
        forge::accel::mock::submit(q, [&] {
            value = 42;
        })).has_value());

    auto events = trace.snapshot();
    assert(events.size() == 3);
    assert(events[0].kind == forge::accel::mock::trace_event_kind::submitted);
    assert(events[1].kind == forge::accel::mock::trace_event_kind::started);
    assert(events[2].kind == forge::accel::mock::trace_event_kind::completed);
    assert(events[0].context.value != 0);
    assert(events[0].stream.value != 0);
    assert(value == 42);

    std::cout << "command timeline events: " << events.size() << "\n";
    return 0;
}
