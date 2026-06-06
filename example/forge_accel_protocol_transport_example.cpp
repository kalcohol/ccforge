#include <forge/accel.hpp>

#include <cassert>
#include "example_support.hpp"
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

    auto posted = transport.submit_posted(request);
    forge_example::require(posted);
    forge_example::require(posted.mode == forge::accel::call_mode::posted);

    auto queued = transport.try_recv_request();
    forge_example::require(queued.has_value());
    auto response = forge::accel::make_response_envelope(
        *queued,
        forge::accel::protocol_payload{std::byte{0x7f}});
    forge_example::require(transport.deliver_response(std::move(response)));

    auto completion = transport.try_recv_completion();
    forge_example::require(completion.has_value());
    forge_example::require(completion->kind == forge::accel::message_kind::response);
    forge_example::require(completion->meta.request.value == 10);
    forge_example::require(completion->payload.size() == 1);

    auto late_response = forge::accel::make_response_envelope(
        *queued,
        forge::accel::protocol_payload{std::byte{0x55}});
    forge_example::require(!transport.deliver_response(std::move(late_response)));
    forge_example::require(transport.late_response_count() == 1);

    auto non_posted = forge::accel::make_request_envelope(
        forge::accel::protocol_route{
            .source = forge::accel::endpoint_id{1},
            .destination = forge::accel::endpoint_id{2},
        },
        forge::accel::protocol_meta{
            .request = forge::accel::request_id{2},
            .session = forge::accel::session_id{7},
        },
        forge::accel::module_id{5},
        forge::accel::command_id{12});
    auto accepted = transport.submit_non_posted(std::move(non_posted));
    forge_example::require(accepted);
    forge_example::require(accepted.mode == forge::accel::call_mode::non_posted);

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
    forge_example::require(transport.deliver_signal(std::move(signal)));
    forge_example::require(transport.try_recv_completion()->kind == forge::accel::message_kind::signal);

    std::cout << "protocol request " << completion->meta.request.value
              << " completed through loopback transport\n";
}
