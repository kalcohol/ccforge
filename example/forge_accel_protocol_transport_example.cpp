#include <forge/accel.hpp>

#include <cassert>
#include <cstddef>
#include <iostream>
#include <vector>

int main() {
    forge::accel::mock::protocol::loopback_transport transport;

    forge::accel::protocol_payload request_payload{
        std::byte{0x01},
        std::byte{0x02},
    };
    auto request = forge::accel::make_request_envelope(
        forge::accel::protocol_route{
            .source = forge::accel::endpoint_id{1},
            .destination = forge::accel::endpoint_id{2},
        },
        forge::accel::protocol_meta{
            .request = forge::accel::request_id{10},
            .session = forge::accel::session_id{20},
            .context = forge::accel::context_id{30},
            .stream = forge::accel::stream_id{40},
        },
        forge::accel::module_id{3},
        forge::accel::command_id{4},
        std::move(request_payload));

    assert(transport.submit_request(request));

    auto queued = transport.try_recv_request();
    assert(queued.has_value());
    auto response = forge::accel::make_response_envelope(
        *queued,
        forge::accel::protocol_payload{std::byte{0x7f}});
    assert(transport.deliver_response(std::move(response)));

    auto completion = transport.try_recv_completion();
    assert(completion.has_value());
    assert(completion->kind == forge::accel::message_kind::response);
    assert(completion->meta.request.value == 10);
    assert(completion->payload.size() == 1);

    auto signal = forge::accel::make_signal_envelope(
        forge::accel::protocol_route{
            .source = forge::accel::endpoint_id{2},
            .destination = forge::accel::endpoint_id{1},
        },
        forge::accel::protocol_meta{},
        forge::accel::lifecycle_signal{
            .reason = forge::accel::lifecycle_signal_reason::closing,
            .diagnostic = "transport closing",
        });
    assert(transport.deliver_signal(std::move(signal)));
    assert(transport.try_recv_completion()->kind == forge::accel::message_kind::signal);

    std::cout << "protocol request " << completion->meta.request.value
              << " completed through loopback transport\n";
}
