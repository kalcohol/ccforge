// MIT License
//
// Copyright (c) 2026 CC Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "vocabulary.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace forge::accel {

struct endpoint_id {
    std::uint64_t value = 0;

    friend auto operator==(endpoint_id, endpoint_id) -> bool = default;
};

struct module_id {
    std::uint64_t value = 0;

    friend auto operator==(module_id, module_id) -> bool = default;
};

enum class message_kind {
    request,
    response,
    notify,
    signal
};

enum class call_mode {
    posted,
    non_posted
};

enum class transport_status {
    ok,
    invalid_message,
    duplicate_request,
    not_accepted,
    late_response
};

enum class lifecycle_signal_reason {
    opened,
    closing,
    closed,
    heartbeat,
    heartbeat_timeout,
    reset,
    device_lost,
    host_lost,
    drain_freeze,
    drain_complete,
    resume,
    worker_fault
};

struct protocol_route {
    endpoint_id source{};
    endpoint_id destination{};

    friend auto operator==(protocol_route, protocol_route) -> bool = default;
};

struct protocol_meta {
    request_id request{};
    session_id session{};
    context_id context{};
    stream_id stream{};

    friend auto operator==(protocol_meta, protocol_meta) -> bool = default;
};

using protocol_payload = std::vector<std::byte>;

struct lifecycle_signal {
    lifecycle_signal_reason reason = lifecycle_signal_reason::closed;
    device_epoch epoch{};
    worker_generation generation{};
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
    std::string diagnostic;
};

struct transport_result {
    transport_status status = transport_status::ok;
    call_mode mode = call_mode::posted;
    request_id request{};

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return status == transport_status::ok;
    }
};

struct protocol_envelope {
    message_kind kind = message_kind::notify;
    protocol_route route{};
    protocol_meta meta{};
    module_id module{};
    command_id command{};
    protocol_payload payload;
    std::optional<lifecycle_signal> signal;
};

[[nodiscard]] inline auto make_request_envelope(
    protocol_route route,
    protocol_meta meta,
    module_id module,
    command_id command,
    protocol_payload payload = {}) -> protocol_envelope {
    return protocol_envelope{
        message_kind::request,
        route,
        meta,
        module,
        command,
        std::move(payload),
        std::nullopt};
}

[[nodiscard]] inline auto make_response_envelope(
    const protocol_envelope& request,
    protocol_payload payload = {}) -> protocol_envelope {
    return protocol_envelope{
        message_kind::response,
        protocol_route{
            .source = request.route.destination,
            .destination = request.route.source,
        },
        request.meta,
        request.module,
        request.command,
        std::move(payload),
        std::nullopt};
}

[[nodiscard]] inline auto make_signal_envelope(
    protocol_route route,
    protocol_meta meta,
    lifecycle_signal signal) -> protocol_envelope {
    return protocol_envelope{
        message_kind::signal,
        route,
        meta,
        module_id{},
        command_id{},
        protocol_payload{},
        std::move(signal)};
}

} // namespace forge::accel
