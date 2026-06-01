#include <forge/accel.hpp>
#include <execution>
#include <iostream>

int main() {
    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = 4,
    }};

    auto session = ctx.get_device().open_session();
    session.reset();

    bool ran = false;
    auto result = std::execution::sync_wait(
        forge::accel::mock::submit(session, [&] {
            ran = true;
        }));

    if (result.has_value() || ran || !session.reset_requested()) {
        return 1;
    }

    std::cout << "session reset stopped a new command\n";
    return 0;
}
