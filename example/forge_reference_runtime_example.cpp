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
#include <array>
#include <cassert>
#include <cstddef>
#include <memory_resource>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

struct inference_request {
    int id = 0;
    std::array<float, 4> features{};
};

struct inference_response {
    int id = 0;
    float score = 0.0f;
    bool ok = true;
    forge::accel::error_kind error = forge::accel::error_kind::unknown;
};

struct runtime_stats {
    int processed = 0;
    float total_score = 0.0f;
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
              .thread_count = 2,
              .queue_capacity = 32,
              .memory = &arena_,
          })
        , accel_(forge::accel::mock::context_options{
              .thread_count = 1,
              .queue_capacity = 16,
              .memory = &arena_,
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
        , accel_queue_(accel_.get_queue())
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
                if (response.ok) {
                    ++stats_.processed;
                    stats_.total_score += response.score;
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
    forge::accel::mock::context accel_;
    forge::bounded_channel<inference_request> requests_;
    forge::bounded_channel<inference_response> responses_;
    forge::strand result_order_;
    forge::accel::mock::queue accel_queue_;
    runtime_stats stats_{};
    bool started_ = false;
};

int main() {
    reference_runtime service;
    assert(service.start());

    assert(service.submit(
        inference_request{.id = 1, .features = {1.0f, 2.0f, 3.0f, 4.0f}}));
    assert(service.submit(
        inference_request{.id = 2, .features = {2.0f, 3.0f, 4.0f, 5.0f}}));
    assert(service.submit(
        inference_request{.id = 3, .features = {3.0f, 4.0f, 5.0f, 6.0f}}));
    service.close();

    std::vector<inference_response> responses;
    while (auto response = service.next_response()) {
        responses.push_back(*response);
    }
    service.wait();

    assert(responses.size() == 3);
    assert(
        responses[0].id == 1 && responses[0].ok &&
        responses[0].score == 34.0f);
    assert(
        responses[1].id == 2 && responses[1].ok &&
        responses[1].score == 58.0f);
    assert(
        responses[2].id == 3 && responses[2].ok &&
        responses[2].score == 90.0f);

    const auto stats = service.stats();
    assert(stats.processed == 3);
    assert(stats.total_score == 182.0f);
}
