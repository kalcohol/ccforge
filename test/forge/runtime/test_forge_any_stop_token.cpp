#include <gtest/gtest.h>

#include <forge/any_stop_token.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace {

struct stop_callback_probe {
    void operator()() noexcept {}
};

static_assert(std::stoppable_token<forge::any_stop_token>);
static_assert(
    std::stoppable_token_for<forge::any_stop_token, stop_callback_probe>);

} // namespace

TEST(ForgeAnyStopTokenTest, DefaultTokenCannotStop) {
    forge::any_stop_token token;

    EXPECT_FALSE(token.stop_requested());
    EXPECT_FALSE(token.stop_possible());
}

TEST(ForgeAnyStopTokenTest, ErasesAndSharesAnInplaceToken) {
    std::inplace_stop_source source;
    forge::any_stop_token token{source.get_token()};
    auto copy = token;

    EXPECT_TRUE(token.stop_possible());
    EXPECT_FALSE(copy.stop_requested());
    EXPECT_TRUE(source.request_stop());
    EXPECT_TRUE(copy.stop_requested());
}

TEST(ForgeAnyStopTokenTest, CallbackRunsExactlyOnce) {
    std::inplace_stop_source source;
    forge::any_stop_token token{source.get_token()};
    int calls = 0;
    auto fn = [&] noexcept { ++calls; };
    std::stop_callback_for_t<forge::any_stop_token, decltype(fn)> callback(
        token,
        fn);

    EXPECT_TRUE(source.request_stop());
    EXPECT_FALSE(source.request_stop());
    EXPECT_EQ(calls, 1);
}

TEST(ForgeAnyStopTokenTest, CallbackDestructionWaitsForInvocation) {
    std::inplace_stop_source source;
    forge::any_stop_token token{source.get_token()};

    std::mutex mtx;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    std::atomic<bool> destructor_returned = false;

    auto fn = [&] noexcept {
        std::unique_lock lock{mtx};
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release; });
    };
    using callback_t =
        std::stop_callback_for_t<forge::any_stop_token, decltype(fn)>;
    auto callback = std::make_unique<callback_t>(token, fn);

    std::thread requester{[&] {
        EXPECT_TRUE(source.request_stop());
    }};
    {
        std::unique_lock lock{mtx};
        cv.wait(lock, [&] { return entered; });
    }

    std::thread destroyer{[&] {
        callback.reset();
        destructor_returned.store(true, std::memory_order_release);
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(destructor_returned.load(std::memory_order_acquire));
    {
        std::lock_guard lock{mtx};
        release = true;
    }
    cv.notify_all();

    destroyer.join();
    requester.join();
    EXPECT_TRUE(destructor_returned.load(std::memory_order_acquire));
}
