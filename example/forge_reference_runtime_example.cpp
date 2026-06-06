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

#include <forge/accel.hpp>
#include <forge/channel.hpp>
#include <forge/resource_context.hpp>
#include <forge/strand.hpp>
#include <forge/wait_result.hpp>
#include <execution>
#include <algorithm>
#include <array>
#include <cassert>
#include "example_support.hpp"
#include <cstddef>
#include <memory_resource>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

enum class request_mode {
    normal,
    size_mismatch,
    device_loss
};

struct inference_request {
    int id = 0;
    std::array<float, 4> features{};
    request_mode mode = request_mode::normal;
};

struct inference_response {
    int id = 0;
    float score = 0.0f;
    bool ok = true;
    forge::accel::error_kind error = forge::accel::error_kind::unknown;
};

struct runtime_stats {
    int responses = 0;
    int processed = 0;
    int errors = 0;
    float total_score = 0.0f;
    forge::accel::error_kind last_error = forge::accel::error_kind::unknown;
};

class reference_runtime {
public:
    reference_runtime()
        : upstream_(
              storage_.data(),
              storage_.size(),
              std::pmr::new_delete_resource())
        , arena_(std::pmr::pool_options{}, &upstream_)
        , runtime_(forge::resource_context_options{
              // The request worker waits at channel boundaries and records
              // ordered results through a synchronous strand hop.
              .thread_count = 2,
              .queue_capacity = 32,
              .memory = &arena_,
          })
        , trace_{&arena_}
        , accel_(forge::accel::mock::context_options{
              .thread_count = 1,
              .queue_capacity = 16,
              .memory = &arena_,
              .trace = &trace_,
          })
        , requests_(forge::bounded_channel_options{
              .capacity = 8,
              .memory = &arena_,
          })
        , responses_(forge::bounded_channel_options{
              .capacity = 2,
              .memory = &arena_,
          })
        , result_order_(
              runtime_.get_scheduler(),
              forge::strand_options{.memory = &arena_})
        , accel_device_(accel_.get_device())
        , accel_queue_(accel_device_.get_queue())
    {}

    ~reference_runtime() noexcept {
        shutdown();
        wait();
    }

    reference_runtime(const reference_runtime&) = delete;
    reference_runtime& operator=(const reference_runtime&) = delete;
    reference_runtime(reference_runtime&&) = delete;
    reference_runtime& operator=(reference_runtime&&) = delete;

    [[nodiscard]] bool start() {
        if (started_) {
            return false;
        }
        started_ = runtime_.spawn(
            std::execution::schedule(runtime_.get_scheduler())
            | std::execution::then([this] { worker_loop(); }));
        return started_;
    }

    [[nodiscard]] bool submit(inference_request request) {
        auto done = std::execution::sync_wait(
            requests_.async_send(std::move(request)));
        return done.has_value();
    }

    void close() noexcept {
        requests_.close();
    }

    void shutdown() noexcept {
        requests_.shutdown();
        responses_.shutdown();
        runtime_.shutdown();
        accel_.shutdown();
    }

    void wait() noexcept {
        runtime_.wait();
        accel_.wait();
        result_order_.wait();
    }

    [[nodiscard]] auto next_response() -> std::optional<inference_response> {
        auto item = std::execution::sync_wait(responses_.async_recv());
        if (!item) {
            return std::nullopt;
        }
        return std::move(std::get<0>(*item));
    }

    [[nodiscard]] auto stats() const noexcept -> runtime_stats {
        return stats_;
    }

    [[nodiscard]] auto trace_snapshot() const
        -> std::vector<forge::accel::mock::trace_event> {
        return trace_.snapshot();
    }

private:
    template<class Sender>
    [[nodiscard]] bool wait_accel(Sender&& sender, inference_response& response) {
        auto result = forge::wait_result(static_cast<Sender&&>(sender));
        if (result.has_value()) {
            return true;
        }
        response.ok = false;
        if (auto* error = result.template error_if<forge::accel::error>()) {
            response.error = error->kind;
        }
        return false;
    }

    [[nodiscard]] auto evaluate(const inference_request& request)
        -> inference_response {
        inference_response response{.id = request.id};

        forge::accel::mock::device_buffer<float> device{accel_, request.features.size()};
        forge::accel::mock::host_buffer<float> output{accel_, request.features.size()};

        if (request.mode == request_mode::size_mismatch) {
            forge::accel::mock::device_buffer<float> too_small{accel_, 1};
            (void)wait_accel(
                forge::accel::mock::copy_to_device_typed(
                    accel_queue_,
                    too_small,
                    std::span<const float>{request.features}),
                response);
            return response;
        }

        if (request.mode == request_mode::device_loss) {
            accel_device_.mark_lost();
            (void)wait_accel(
                forge::accel::mock::copy_to_device_typed(
                    accel_queue_,
                    device,
                    std::span<const float>{request.features}),
                response);
            accel_device_.reset();
            return response;
        }

        if (!wait_accel(
                forge::accel::mock::copy_to_device_typed(
                    accel_queue_,
                    device,
                    std::span<const float>{request.features}),
                response)) {
            return response;
        }

        if (!wait_accel(
                forge::accel::mock::submit_typed(accel_queue_, [&] {
                    for (auto& value : device.span()) {
                        value = value * value + 1.0f;
                    }
                }),
                response)) {
            return response;
        }

        if (!wait_accel(
                forge::accel::mock::copy_to_host_typed(
                    accel_queue_,
                    output.span(),
                    device),
                response)) {
            return response;
        }

        for (float value : output.span()) {
            response.score += value;
        }
        return response;
    }

    [[nodiscard]] bool record(const inference_response& response) {
        auto done = std::execution::sync_wait(
            std::execution::schedule(result_order_.get_scheduler())
            | std::execution::then([this, response] {
                ++stats_.responses;
                if (response.ok) {
                    ++stats_.processed;
                    stats_.total_score += response.score;
                } else {
                    ++stats_.errors;
                    stats_.last_error = response.error;
                }
            }));
        return done.has_value();
    }

    void worker_loop() {
        while (auto item = std::execution::sync_wait(requests_.async_recv())) {
            auto request = std::move(std::get<0>(*item));
            auto response = evaluate(request);
            if (!record(response)) {
                return;
            }
            auto sent = std::execution::sync_wait(
                responses_.async_send(std::move(response)));
            if (!sent) {
                return;
            }
        }
        responses_.close();
    }

    std::array<std::byte, 32768> storage_{};
    std::pmr::monotonic_buffer_resource upstream_;
    std::pmr::synchronized_pool_resource arena_;
    forge::resource_context runtime_;
    forge::accel::mock::trace_sink trace_;
    forge::accel::mock::context accel_;
    forge::bounded_channel<inference_request> requests_;
    forge::bounded_channel<inference_response> responses_;
    forge::strand result_order_;
    forge::accel::mock::device accel_device_;
    forge::accel::mock::queue accel_queue_;
    runtime_stats stats_{};
    bool started_ = false;
};

int main() {
    reference_runtime service;
    forge_example::require(service.start());

    forge_example::require(service.submit(
        inference_request{.id = 1, .features = {1.0f, 2.0f, 3.0f, 4.0f}}));
    forge_example::require(service.submit(
        inference_request{
            .id = 2,
            .features = {2.0f, 3.0f, 4.0f, 5.0f},
            .mode = request_mode::size_mismatch}));
    forge_example::require(service.submit(
        inference_request{
            .id = 3,
            .features = {3.0f, 4.0f, 5.0f, 6.0f},
            .mode = request_mode::device_loss}));
    forge_example::require(service.submit(
        inference_request{.id = 4, .features = {2.0f, 3.0f, 4.0f, 5.0f}}));
    service.close();
    forge_example::require(!service.submit(inference_request{
        .id = 5,
        .features = {1.0f, 1.0f, 1.0f, 1.0f}}));

    std::vector<inference_response> responses;
    while (auto response = service.next_response()) {
        responses.push_back(*response);
    }
    service.wait();

    forge_example::require(responses.size() == 4);
    forge_example::require(
        responses[0].id == 1 && responses[0].ok &&
        responses[0].score == 34.0f);
    forge_example::require(!responses[1].ok);
    forge_example::require(responses[1].error == forge::accel::error_kind::size_mismatch);
    forge_example::require(!responses[2].ok);
    forge_example::require(responses[2].error == forge::accel::error_kind::device_lost);
    forge_example::require(
        responses[3].id == 4 && responses[3].ok &&
        responses[3].score == 58.0f);

    const auto stats = service.stats();
    forge_example::require(stats.responses == 4);
    forge_example::require(stats.processed == 2);
    forge_example::require(stats.errors == 2);
    forge_example::require(stats.total_score == 92.0f);
    forge_example::require(stats.last_error == forge::accel::error_kind::device_lost);

    const auto trace = service.trace_snapshot();
    forge_example::require(trace.size() >= 12);
    forge_example::require(std::any_of(trace.begin(), trace.end(), [](const auto& event) {
        return event.kind == forge::accel::mock::trace_event_kind::device_lost;
    }));

    reference_runtime stopped;
    forge_example::require(stopped.start());
    stopped.shutdown();
    stopped.wait();
    forge_example::require(!stopped.submit(inference_request{
        .id = 6,
        .features = {1.0f, 1.0f, 1.0f, 1.0f}}));
}
