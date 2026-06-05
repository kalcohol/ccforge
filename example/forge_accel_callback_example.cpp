#include <forge/accel.hpp>
#include <execution>
#include <cassert>
#include <vector>

int main() {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 2,
    }};
    forge::accel::mock::host_callback_dispatcher_options callback_options;
    callback_options.completion_capacity = 4;
    forge::accel::mock::host_callback_dispatcher callbacks{callback_options};
    auto q = ctx.get_queue(forge::accel::queue_kind::compute);

    std::vector<int> order;
    auto callback = callbacks.register_callback([&] {
        order.push_back(2);
    });

    std::execution::sync_wait(forge::accel::mock::submit(q, [&] {
        order.push_back(1);
    }));
    std::execution::sync_wait(
        forge::accel::mock::enqueue_callback(q, callbacks, callback));
    std::execution::sync_wait(forge::accel::mock::submit(q, [&] {
        order.push_back(3);
    }));

    assert((order == std::vector<int>{1, 2, 3}));
    auto completions = callbacks.completions();
    assert(completions.size() == 1);
    assert(completions[0].callback == callback);
    assert(completions[0].status == forge::accel::callback_status::ok);

    callbacks.shutdown();
    callbacks.wait();
}
