#include <forge/accel.hpp>
#include <execution>
#include <chrono>
#include <iostream>
#include <tuple>

struct request_packet {
    int token_count = 0;
};

struct response_packet {
    int logits_ready = 0;
};

int main() {
    using namespace std::chrono_literals;
    using packet_t = forge::accel::mock::command_packet<
        request_packet,
        response_packet>;

    forge::accel::mock::context ctx{forge::accel::mock::context_options{
        .thread_count = 1,
        .queue_capacity = 8,
    }};

    auto session = ctx.get_device().open_session();
    auto result = std::execution::sync_wait(
        forge::accel::mock::submit_packet(
            session,
            packet_t{
                forge::accel::command_id{1},
                request_packet{128},
                response_packet{}},
            [](request_packet& request, response_packet& out) noexcept {
                out.logits_ready = request.token_count;
                return forge::accel::command_status::ok;
            },
            forge::accel::mock::command_options{.timeout = 1s}));

    if (!result) {
        return 1;
    }

    auto& packet = std::get<0>(*result);
    if (packet.id.value != 1 || packet.response.logits_ready != 128) {
        return 1;
    }

    std::cout << "owning packet " << packet.id.value
              << " completed for " << packet.response.logits_ready
              << " tokens\n";
    return 0;
}
