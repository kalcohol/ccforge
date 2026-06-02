#include <forge/accel.hpp>
#include <forge/wait_result.hpp>

#include <cassert>
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

    assert(sync.has_value());
    auto sync_packet = std::get<0>(std::move(sync.value()));
    assert(sync_packet.id.value == 1);
    assert(sync_packet.response == 42);

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

    std::execution::start_detached(std::move(posted));

    {
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return done; });
    }

    assert(posted_response == 12);
    assert(requests.pending_count() == 0);
}
