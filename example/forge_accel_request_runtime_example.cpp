#include <forge/accel.hpp>
#include <forge/start_detached.hpp>
#include <forge/wait_result.hpp>

#include <cassert>
#include "example_support.hpp"
#include <condition_variable>
#include <execution>
#include <mutex>

int main() {
    forge::accel::mock::context ctx;
    auto device = ctx.get_device();
    forge::accel::mock::request_session requests{device.open_session()};

    auto sync = forge::wait_result(
        requests.submit_request(
            21,
            0,
            [](int& request, int& response) noexcept {
                response = request * 2;
            }));

    forge_example::require(sync.has_value());
    auto sync_packet = std::get<0>(std::move(sync.value()));
    forge_example::require(sync_packet.id.value == 1);
    forge_example::require(sync_packet.response == 42);

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    int posted_response = 0;

    auto posted = requests.submit_request(
        7,
        0,
        [](int& request, int& response) noexcept {
            response = request + 5;
        })
        | std::execution::then([&](auto packet) noexcept {
              {
                  std::lock_guard lk{mtx};
                  posted_response = packet.response;
                  done = true;
              }
              cv.notify_all();
          })
        | std::execution::upon_error([&](std::exception_ptr) noexcept {
              {
                  std::lock_guard lk{mtx};
                  done = true;
              }
              cv.notify_all();
          })
        | std::execution::upon_stopped([&] noexcept {
              {
                  std::lock_guard lk{mtx};
                  done = true;
              }
              cv.notify_all();
          });

    forge::start_detached(std::move(posted));

    {
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return done; });
    }

    forge_example::require(posted_response == 12);
    forge_example::require(requests.pending_count() == 0);

    forge::accel::mock::context failure_ctx;
    auto failure_device = failure_ctx.get_device();
    forge::accel::mock::request_session failing_requests{
        failure_device.open_session()};
    failure_device.mark_lost();

    auto failure = forge::wait_result(
        failing_requests.submit_request_typed(
            1,
            0,
            [](int& request, int& response) noexcept {
                response = request;
            }));
    forge_example::require(failure.has_error());
    auto* error = failure.error_if<forge::accel::error>();
    forge_example::require(error != nullptr);
    forge_example::require(error->kind == forge::accel::error_kind::device_lost);
}
