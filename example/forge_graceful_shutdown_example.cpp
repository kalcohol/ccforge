// MIT License
//
// Copyright (c) 2026 Forge Project
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

#include <forge/channel.hpp>
#include <forge/resource_context.hpp>
#include <forge/strand.hpp>

#include <execution>
#include <cassert>
#include "example_support.hpp"
#include <tuple>
#include <vector>

namespace {

struct request {
    int id;
    int payload;
};

class service {
public:
    service()
        : ctx_(forge::resource_context_options{
              // run() blocks on async_recv() and then performs a synchronous
              // strand hop, so this example budgets a spare worker.
              .thread_count = 2,
              .queue_capacity = 16,
          })
        , requests_(forge::bounded_channel_options{.capacity = 4})
        , serial_(ctx_.get_scheduler()) {
        bool spawned = ctx_.spawn(
            std::execution::schedule(ctx_.get_scheduler())
            | std::execution::then([this] noexcept { run(); }));
        forge_example::require(spawned);
    }

    ~service() noexcept {
        stop_and_wait();
    }

    bool submit(request req) {
        auto accepted = std::execution::sync_wait(requests_.async_send(req));
        return accepted.has_value();
    }

    void close_and_wait() {
        requests_.close();
        ctx_.close();
        ctx_.wait();
        serial_.wait();
    }

    void stop_and_wait() {
        requests_.request_stop();
        ctx_.shutdown();
        ctx_.wait();
        serial_.wait();
    }

    const std::vector<int>& results() const noexcept {
        return results_;
    }

private:
    void run() noexcept {
        while (auto item = std::execution::sync_wait(requests_.async_recv())) {
            auto req = std::get<0>(*item);
            (void)std::execution::sync_wait(
                std::execution::schedule(serial_.get_scheduler())
                | std::execution::then([this, req] noexcept {
                    results_.push_back(req.id * 100 + req.payload);
                }));
        }
    }

    forge::resource_context ctx_;
    forge::bounded_channel<request> requests_;
    forge::strand serial_;
    std::vector<int> results_;
};

} // namespace

int main() {
    {
        service svc;
        forge_example::require(svc.submit({1, 7}));
        forge_example::require(svc.submit({2, 9}));

        svc.close_and_wait();

        forge_example::require((svc.results() == std::vector<int>{107, 209}));
        forge_example::require(!svc.submit({3, 11}));
    }

    {
        service svc;
        svc.stop_and_wait();
        forge_example::require(svc.results().empty());
        forge_example::require(!svc.submit({1, 1}));
    }
}
