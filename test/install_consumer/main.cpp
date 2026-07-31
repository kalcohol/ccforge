#include <forge/erased_sender.hpp>
#include <forge/execution.hpp>
#include <forge/static_thread_pool.hpp>

#include <execution>
#include <exception>
#include <system_error>
#include <tuple>

namespace {

struct error_code_receiver {
    using receiver_concept = std::execution::receiver_t;

    bool* seen{};

    void set_value() && noexcept {}
    void set_error(std::error_code ec) && noexcept {
        *seen = (ec == std::make_error_code(std::errc::timed_out));
    }
    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct error_code_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_error_t(std::error_code)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class Receiver>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        Receiver rcvr;

        void start() & noexcept {
            std::execution::set_error(
                std::move(rcvr),
                std::make_error_code(std::errc::timed_out));
        }
    };

    template<class Receiver>
    auto connect(Receiver rcvr) & -> op<Receiver> {
        return op<Receiver>{std::move(rcvr)};
    }
};

} // namespace

int main() {
    auto just_result = std::this_thread::sync_wait(std::execution::just(42));
    if (!just_result || std::get<0>(*just_result) != 42) {
        return 1;
    }

    forge::static_thread_pool pool{1};
    auto scheduled = std::this_thread::sync_wait(
        std::execution::schedule(pool.get_scheduler()));
    pool.shutdown();
    pool.wait();
    if (!scheduled) {
        return 2;
    }

    using typed_error_cs =
        std::execution::completion_signatures<
            std::execution::set_error_t(std::error_code)>;
    forge::erased_sender<typed_error_cs> erased{error_code_sender{}};
    bool typed_error_seen = false;
    auto op = std::execution::connect(
        std::move(erased),
        error_code_receiver{&typed_error_seen});
    std::execution::start(op);
    if (!typed_error_seen) {
        return 3;
    }

    return 0;
}
