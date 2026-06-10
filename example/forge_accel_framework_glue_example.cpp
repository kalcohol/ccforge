#include <forge/accel.hpp>
#include <execution>
#include "example_support.hpp"
#include <span>
#include <utility>
#include <vector>

struct backend {
    forge::accel::mock::context context;
    forge::accel::mock::queue compute =
        context.get_queue(forge::accel::queue_kind::compute);

    template<class F>
    void run_on_current_stream(F&& op) {
        auto device = forge::accel::current_device();
        auto stream = forge::accel::mock::query_stream(compute).stream;
        std::execution::sync_wait(
            forge::accel::mock::submit(
                compute,
                [device, stream, op = std::forward<F>(op)]() mutable {
                    forge::accel::current_stream_guard stream_guard{stream};
                    if (device) {
                        forge::accel::current_device_guard device_guard{*device};
                        op();
                    } else {
                        op();
                    }
                }));
    }
};

int main() {
    backend b;
    forge::accel::current_device_guard device_guard{forge::accel::device_id{0}};
    forge::accel::mock::device_buffer<int> data{b.context, 4};
    std::vector<int> host{1, 2, 3, 4};

    std::execution::sync_wait(
        forge::accel::mock::copy_to_device(
            b.compute,
            data,
            std::span<const int>{host}));

    b.run_on_current_stream([&] {
        forge_example::require(forge::accel::current_device().value().value == 0);
        forge_example::require(forge::accel::current_stream().has_value());
        for (auto& value : data.span()) {
            value *= 2;
        }
    });

    auto sync = forge::accel::mock::synchronize_stream(b.compute);
    forge_example::require(sync);

    std::vector<int> out(4);
    std::execution::sync_wait(
        forge::accel::mock::copy_to_host(b.compute, std::span<int>{out}, data));
    forge_example::require((out == std::vector<int>{2, 4, 6, 8}));
}
