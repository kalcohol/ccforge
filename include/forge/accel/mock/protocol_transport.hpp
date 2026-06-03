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

#include "../protocol.hpp"
#include "../../channel.hpp"
#include "../../resource_policy.hpp"

#include <cstddef>
#include <memory_resource>
#include <mutex>
#include <unordered_set>
#include <utility>

namespace forge::accel::mock::protocol {

struct loopback_transport_options {
    std::size_t capacity = 16;
    std::pmr::memory_resource* memory = forge::default_memory_resource();
};

class loopback_transport {
public:
    explicit loopback_transport(loopback_transport_options options = {})
        : memory_(forge::normalize_memory_resource(options.memory))
        , pending_(std::pmr::polymorphic_allocator<std::uint64_t>{memory_})
        , requests_(forge::bounded_channel_options{options.capacity, memory_})
        , completions_(forge::bounded_channel_options{options.capacity, memory_})
    {}

    loopback_transport(const loopback_transport&) = delete;
    auto operator=(const loopback_transport&) -> loopback_transport& = delete;
    loopback_transport(loopback_transport&&) = delete;
    auto operator=(loopback_transport&&) -> loopback_transport& = delete;

    ~loopback_transport() noexcept {
        shutdown();
    }

    [[nodiscard]] bool submit_request(protocol_envelope envelope) {
        return static_cast<bool>(submit_posted(std::move(envelope)));
    }

    [[nodiscard]] auto submit_posted(protocol_envelope envelope) -> transport_result {
        return submit_request(std::move(envelope), call_mode::posted);
    }

    [[nodiscard]] auto submit_non_posted(protocol_envelope envelope) -> transport_result {
        return submit_request(std::move(envelope), call_mode::non_posted);
    }

    [[nodiscard]] auto submit_request(protocol_envelope envelope, call_mode mode)
        -> transport_result {
        if (envelope.kind != message_kind::request || envelope.meta.request.value == 0) {
            return transport_result{
                transport_status::invalid_message,
                mode,
                envelope.meta.request};
        }

        const auto id = envelope.meta.request.value;
        {
            std::lock_guard lk{mtx_};
            if (!pending_.insert(id).second) {
                return transport_result{
                    transport_status::duplicate_request,
                    mode,
                    envelope.meta.request};
            }
        }

        const auto request = envelope.meta.request;
        if (requests_.try_send(std::move(envelope))) {
            return transport_result{transport_status::ok, mode, request};
        }

        std::lock_guard lk{mtx_};
        pending_.erase(id);
        return transport_result{transport_status::not_accepted, mode, request};
    }

    [[nodiscard]] auto try_recv_request() -> std::optional<protocol_envelope> {
        return requests_.try_recv();
    }

    [[nodiscard]] bool deliver_response(protocol_envelope envelope) {
        return static_cast<bool>(deliver_response_result(std::move(envelope)));
    }

    [[nodiscard]] auto deliver_response_result(protocol_envelope envelope)
        -> transport_result {
        if (envelope.kind != message_kind::response || envelope.meta.request.value == 0) {
            return transport_result{
                transport_status::invalid_message,
                call_mode::posted,
                envelope.meta.request};
        }

        std::lock_guard lk{mtx_};
        auto it = pending_.find(envelope.meta.request.value);
        if (it == pending_.end()) {
            ++late_responses_;
            return transport_result{
                transport_status::late_response,
                call_mode::posted,
                envelope.meta.request};
        }

        const auto request = envelope.meta.request;
        if (!completions_.try_send(std::move(envelope))) {
            return transport_result{
                transport_status::not_accepted,
                call_mode::posted,
                request};
        }
        pending_.erase(it);
        return transport_result{transport_status::ok, call_mode::posted, request};
    }

    [[nodiscard]] bool deliver_signal(protocol_envelope envelope) {
        if (envelope.kind != message_kind::signal || !envelope.signal) {
            return false;
        }
        return completions_.try_send(std::move(envelope));
    }

    [[nodiscard]] auto try_recv_completion() -> std::optional<protocol_envelope> {
        return completions_.try_recv();
    }

    [[nodiscard]] auto pending_count() const noexcept -> std::size_t {
        std::lock_guard lk{mtx_};
        return pending_.size();
    }

    [[nodiscard]] auto late_response_count() const noexcept -> std::size_t {
        std::lock_guard lk{mtx_};
        return late_responses_;
    }

    void close() noexcept {
        requests_.close();
        completions_.close();
    }

    void request_stop() noexcept {
        requests_.request_stop();
        completions_.request_stop();
    }

    void shutdown() noexcept {
        requests_.shutdown();
        completions_.shutdown();
    }

private:
    std::pmr::memory_resource* memory_;
    mutable std::mutex mtx_;
    std::pmr::unordered_set<std::uint64_t> pending_;
    std::size_t late_responses_ = 0;
    forge::bounded_channel<protocol_envelope> requests_;
    forge::bounded_channel<protocol_envelope> completions_;
};

} // namespace forge::accel::mock::protocol
