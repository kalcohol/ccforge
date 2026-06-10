#include <gtest/gtest.h>

#include <execution>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

struct env_without_stop_query {};

} // namespace

TEST(ExecutionStopTokenTest, InplaceStopCallbackIsInvoked) {
    std::inplace_stop_source src;
    auto token = src.get_token();

    bool called = false;
    std::inplace_stop_callback cb(token, [&] { called = true; });

    EXPECT_FALSE(called);
    EXPECT_TRUE(src.request_stop());
    EXPECT_TRUE(called);
}

TEST(ExecutionStopTokenTest, InplaceStopRequestIsIdempotent) {
    std::inplace_stop_source src;
    EXPECT_TRUE(src.request_stop());
    EXPECT_FALSE(src.request_stop());
}

// ── T-4: Expanded stop-token coverage ───────────────────────────────────

TEST(ExecutionStopTokenTest, PostStopCallbackImmediateInvocation) {
    std::inplace_stop_source src;
    src.request_stop();

    bool called = false;
    std::inplace_stop_callback cb(src.get_token(), [&] { called = true; });
    EXPECT_TRUE(called);
}

TEST(ExecutionStopTokenTest, CallbackAutoDeregisterOnDestruction) {
    std::inplace_stop_source src;
    auto token = src.get_token();

    bool called = false;
    {
        std::inplace_stop_callback cb(token, [&] { called = true; });
    } // cb destroyed — auto-deregistered
    src.request_stop();
    EXPECT_FALSE(called);
}

TEST(ExecutionStopTokenTest, CallbackDestructionWaitsForConcurrentInvocation) {
    std::inplace_stop_source src;
    auto token = src.get_token();

    std::mutex mtx;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    std::atomic<bool> destructor_returned = false;

    auto fn = [&] {
        std::unique_lock lk{mtx};
        entered = true;
        cv.notify_all();
        cv.wait(lk, [&] { return release; });
    };
    using callback_t = std::inplace_stop_callback<decltype(fn)>;
    auto cb = std::make_unique<callback_t>(token, fn);

    std::thread requester{[&] {
        EXPECT_TRUE(src.request_stop());
    }};

    {
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return entered; });
    }

    std::thread destroyer{[&] {
        cb.reset();
        destructor_returned.store(true, std::memory_order_release);
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(destructor_returned.load(std::memory_order_acquire));

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();

    destroyer.join();
    requester.join();
    EXPECT_TRUE(destructor_returned.load(std::memory_order_acquire));
}

TEST(ExecutionStopTokenTest, AnyStopCallbackDestructionWaitsForConcurrentInvocation) {
    std::inplace_stop_source src;
    std::any_stop_token token{src.get_token()};

    std::mutex mtx;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    std::atomic<bool> destructor_returned = false;

    auto fn = [&] {
        std::unique_lock lk{mtx};
        entered = true;
        cv.notify_all();
        cv.wait(lk, [&] { return release; });
    };
    using callback_t = std::stop_callback_for_t<std::any_stop_token, decltype(fn)>;
    auto cb = std::make_unique<callback_t>(token, fn);

    std::thread requester{[&] {
        EXPECT_TRUE(src.request_stop());
    }};

    {
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return entered; });
    }

    std::thread destroyer{[&] {
        cb.reset();
        destructor_returned.store(true, std::memory_order_release);
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(destructor_returned.load(std::memory_order_acquire));

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();

    destroyer.join();
    requester.join();
    EXPECT_TRUE(destructor_returned.load(std::memory_order_acquire));
}

TEST(ExecutionStopTokenTest, AnyStopTokenConcurrentRegisterRequestAndDestroy) {
    constexpr int kIterations = 64;
    constexpr int kLanes = 4;
    constexpr int kCallbacksPerLane = 6;

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        std::inplace_stop_source src;
        std::any_stop_token token{src.get_token()};
        std::atomic<int> ready{0};
        std::atomic<int> registered_lanes{0};
        std::atomic<int> callbacks{0};
        std::atomic<bool> go{false};
        std::atomic<bool> request_done{false};
        std::vector<std::thread> lanes;

        using callback_t =
            std::stop_callback_for_t<std::any_stop_token, std::function<void()>>;

        for (int lane = 0; lane < kLanes; ++lane) {
            lanes.emplace_back([&, lane] {
                ready.fetch_add(1, std::memory_order_acq_rel);
                while (!go.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }

                std::vector<std::unique_ptr<callback_t>> registrations;
                registrations.reserve(kCallbacksPerLane);
                for (int i = 0; i < kCallbacksPerLane; ++i) {
                    registrations.push_back(std::make_unique<callback_t>(
                        token,
                        std::function<void()>{[&] {
                            callbacks.fetch_add(1, std::memory_order_acq_rel);
                        }}));
                    if (((iteration + lane + i) % 3) == 0) {
                        std::this_thread::yield();
                    }
                }
                registered_lanes.fetch_add(1, std::memory_order_acq_rel);

                while (!request_done.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                registrations.clear();
            });
        }

        while (ready.load(std::memory_order_acquire) != kLanes) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);
        while (registered_lanes.load(std::memory_order_acquire) != kLanes) {
            std::this_thread::yield();
        }

        EXPECT_TRUE(src.request_stop());
        request_done.store(true, std::memory_order_release);

        for (auto& lane : lanes) {
            lane.join();
        }
        EXPECT_EQ(callbacks.load(std::memory_order_acquire),
                  kLanes * kCallbacksPerLane);
    }
}

TEST(ExecutionStopTokenTest, CallbackCanDestroyPendingCallbackDuringRequestStop) {
    std::inplace_stop_source src;
    auto token = src.get_token();

    using callback_t = std::inplace_stop_callback<std::function<void()>>;
    std::unique_ptr<callback_t> first;
    std::unique_ptr<callback_t> second;
    bool first_called = false;
    bool second_called = false;

    second = std::make_unique<callback_t>(
        token,
        std::function<void()>{[&] { second_called = true; }});
    first = std::make_unique<callback_t>(
        token,
        std::function<void()>{[&] {
            first_called = true;
            second.reset();
        }});

    EXPECT_TRUE(src.request_stop());
    EXPECT_TRUE(first_called);
    EXPECT_FALSE(second_called);
}

TEST(ExecutionStopTokenTest, NeverStopTokenBehavior) {
    std::never_stop_token token{};
    EXPECT_FALSE(token.stop_requested());
    EXPECT_FALSE(token.stop_possible());
    EXPECT_TRUE(token == std::never_stop_token{});
}

TEST(ExecutionStopTokenTest, MissingEnvStopQueryFallsBackToNeverStopToken) {
    auto token = std::execution::get_stop_token(env_without_stop_query{});

    static_assert(std::is_same_v<decltype(token), std::never_stop_token>);
    EXPECT_FALSE(token.stop_requested());
    EXPECT_FALSE(token.stop_possible());
}

TEST(ExecutionStopTokenTest, DefaultConstructedToken) {
    std::inplace_stop_token token{};
    EXPECT_FALSE(token.stop_requested());
    EXPECT_FALSE(token.stop_possible());
}

// ── T-8: stoppable_token concept probes ──────────────────────────────────

static_assert(std::stoppable_token<std::inplace_stop_token>,
              "inplace_stop_token must satisfy stoppable_token");
static_assert(std::stoppable_token<std::never_stop_token>,
              "never_stop_token must satisfy stoppable_token");

static_assert(std::unstoppable_token<std::never_stop_token>,
              "never_stop_token must satisfy unstoppable_token");
static_assert(!std::unstoppable_token<std::inplace_stop_token>,
              "inplace_stop_token must NOT satisfy unstoppable_token");

static_assert(std::stoppable_token_for<std::inplace_stop_token, void(*)()>,
              "inplace_stop_token must satisfy stoppable_token_for with function pointer");

static_assert(!std::stoppable_token<int>,
              "int must not satisfy stoppable_token");

// ── T-9: sync_wait accessible from std::this_thread ─────────────────────

TEST(ExecutionStopTokenTest, SyncWaitFromThisThread) {
    auto result = std::this_thread::sync_wait(std::execution::just(99));
    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 99);
}
