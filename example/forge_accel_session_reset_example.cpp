#include <forge/accel.hpp>
#include <execution>
#include <iostream>

int main() {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = 4,
    }};

    auto device = ctx.get_device();
    auto session = device.open_session();
    session.reset();

    bool ran = false;
    auto result = std::execution::sync_wait(
        forge::accel::mock::submit(session, [&] {
            ran = true;
        }));

    if (result.has_value() || ran || !session.reset_requested()) {
        return 1;
    }

    auto old_session = device.open_session();
    device.mark_lost();
    try {
        (void)std::execution::sync_wait(
            forge::accel::mock::submit(old_session, [] {}));
        return 1;
    } catch (const forge::accel::operation_error& error) {
        if (error.kind() != forge::accel::error_kind::device_lost) {
            return 1;
        }
    }

    device.reset();
    try {
        (void)std::execution::sync_wait(
            forge::accel::mock::submit(old_session, [] {}));
        return 1;
    } catch (const forge::accel::operation_error& error) {
        if (error.kind() != forge::accel::error_kind::stale_session) {
            return 1;
        }
    }

    auto recovered = device.open_session();
    auto recovered_result = std::execution::sync_wait(
        forge::accel::mock::submit(recovered, [] {}));
    if (!recovered_result.has_value()) {
        return 1;
    }

    std::cout << "session reset, device lost, and recovery boundaries passed\n";
    return 0;
}
