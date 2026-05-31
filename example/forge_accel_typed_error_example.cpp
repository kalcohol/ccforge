#include <forge/accel.hpp>
#include <forge/erased_sender.hpp>
#include <execution>
#include <cassert>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

struct result_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    forge::accel::error error{};
};

struct receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<result_state> state;

    void set_value() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->done = true;
        }
        state->cv.notify_all();
    }

    void set_error(forge::accel::error error) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->done = true;
            state->error = std::move(error);
        }
        state->cv.notify_all();
    }

    void set_stopped() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->done = true;
        }
        state->cv.notify_all();
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

int main() {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::device_buffer<int> device{ctx, 1};
    std::vector<int> input{1, 2};

    using command = std::execution::completion_signatures<
        std::execution::set_value_t(),
        std::execution::set_error_t(forge::accel::error),
        std::execution::set_stopped_t()>;

    forge::erased_sender<command> work{
        forge::accel::copy_to_device_typed(
            q,
            device,
            std::span<const int>{input})};

    auto state = std::make_shared<result_state>();
    auto op = std::execution::connect(std::move(work), receiver{state});
    std::execution::start(op);

    {
        std::unique_lock lk{state->mtx};
        state->cv.wait(lk, [&] { return state->done; });
    }

    assert(state->error.kind == forge::accel::error_kind::size_mismatch);
    assert(state->error.cause);
    try {
        std::rethrow_exception(state->error.cause);
    } catch (const std::runtime_error&) {
        return 0;
    }
    return 1;
}

