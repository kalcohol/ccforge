#include <forge/accel.hpp>
#include <execution>
#include <cassert>

int main() {
    forge::accel::mock::context ctx;
    auto device = ctx.get_device();
    auto old_session = device.open_session();

    assert(std::execution::sync_wait(
        forge::accel::mock::submit(old_session, [] {})).has_value());

    device.begin_host_lost_cleanup();
    device.complete_host_lost_cleanup();

    try {
        (void)std::execution::sync_wait(
            forge::accel::mock::submit(old_session, [] {}));
        assert(false);
    } catch (const forge::accel::operation_error& error) {
        assert(error.kind() == forge::accel::error_kind::stale_session);
    }

    auto resumed_session = device.open_session();
    assert(resumed_session.epoch() == device.epoch());
    assert(std::execution::sync_wait(
        forge::accel::mock::submit(resumed_session, [] {})).has_value());
}
