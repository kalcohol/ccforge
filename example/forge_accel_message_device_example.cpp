#include <forge/accel.hpp>
#include <execution>
#include <iostream>
#include <tuple>

struct request_packet {
    int token_count = 0;
};

struct response_packet {
    int logits_ready = 0;
};

int main() {
    forge::accel::context ctx{forge::accel::context_options{
        .thread_count = 1,
        .queue_capacity = 8,
    }};

    auto device = ctx.get_device();
    auto session = device.open_session();

    response_packet response{};
    auto result = std::execution::sync_wait(
        forge::accel::submit_message(
            session,
            request_packet{128},
            response,
            [](request_packet& request, response_packet& out) noexcept {
                out.logits_ready = request.token_count;
                return forge::accel::command_status::ok;
            }));

    if (!result || response.logits_ready != 128) {
        return 1;
    }

    std::cout << "device response ready for "
              << response.logits_ready << " tokens\n";
    return 0;
}
