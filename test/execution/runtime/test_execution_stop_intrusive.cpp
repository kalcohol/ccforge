#include <gtest/gtest.h>
#include <execution>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <semaphore>
#include <thread>
#include <type_traits>

namespace {
struct nothrow_function {
    void operator()() const noexcept {}
};

struct throwing_initializer {};
struct throwing_function {
    explicit throwing_function(throwing_initializer) { throw 17; }
    void operator()() const noexcept {}
};

struct throwing_invocation {
    explicit throwing_invocation(int) noexcept(false) {}
    void operator()() const { throw 23; }
};

static_assert(std::is_nothrow_constructible_v<
    std::inplace_stop_callback<nothrow_function>, std::inplace_stop_token, nothrow_function>);
static_assert(std::is_nothrow_constructible_v<
    std::inplace_stop_callback<nothrow_function&>, std::inplace_stop_token, nothrow_function&>);
static_assert(!std::is_nothrow_constructible_v<
    std::inplace_stop_callback<throwing_function>, std::inplace_stop_token, throwing_initializer>);
static_assert(!std::is_move_constructible_v<std::inplace_stop_callback<nothrow_function>>);

// The stored function's destructor observes invocation exit without atomics;
// deregistration must provide the synchronization for this plain read.
struct blocking_function {
    std::binary_semaphore* entered;
    std::binary_semaphore* release;
    bool* invocation_finished;
    bool* function_destroyed;

    void operator()() const noexcept {
        entered->release();
        release->acquire();
        *invocation_finished = true;
    }
    ~blocking_function() {
        if (function_destroyed) {
            EXPECT_TRUE(*invocation_finished);
            *function_destroyed = true;
        }
    }
};
} // namespace

TEST(StopIntrusiveTest, ConstructorPropagatesOnlyCallbackInitializationFailure) {
    for (bool stopped : {false, true}) {
        std::inplace_stop_source source;
        if (stopped) source.request_stop();
        try {
            std::inplace_stop_callback<throwing_function> callback(
                source.get_token(), throwing_initializer{});
            FAIL() << "callback initialization should throw";
        } catch (int error) {
            EXPECT_EQ(error, 17);
        }
        int calls = 0;
        std::inplace_stop_callback callback(source.get_token(), [&] { ++calls; });
        EXPECT_EQ(source.request_stop(), !stopped);
        EXPECT_EQ(calls, 1);
    }
}

TEST(StopIntrusiveTest, TokenIdentityAndSwap) {
    std::inplace_stop_source first, second;
    auto token = first.get_token();
    auto copy = token;
    std::inplace_stop_token empty;
    EXPECT_EQ(token, copy);
    EXPECT_NE(token, second.get_token());
    EXPECT_NE(token, empty);
    swap(token, empty);
    EXPECT_EQ(empty, copy);
    EXPECT_EQ(token, std::inplace_stop_token{});
    first.request_stop();
    EXPECT_TRUE(empty.stop_requested());
    EXPECT_FALSE(token.stop_requested());
}

TEST(StopIntrusiveDeathTest, InvocationExceptionsTerminateEvenWithThrowingConstructorTrait) {
    for (bool stopped : {false, true}) {
        ASSERT_EXIT({
            std::set_terminate([] { std::_Exit(86); });
            std::inplace_stop_source source;
            if (stopped) source.request_stop();
            try {
                std::inplace_stop_callback<throwing_invocation> callback(source.get_token(), 0);
                source.request_stop();
            } catch (...) {
                std::_Exit(87);
            }
            std::_Exit(88);
        }, ::testing::ExitedWithCode(86), "");
    }
}

TEST(StopIntrusiveTest, SelfDestructionLeavesRequesterOnlyStackState) {
    std::inplace_stop_source source;
    using callback_t = std::inplace_stop_callback<std::function<void()>>;
    std::unique_ptr<callback_t> self;
    int calls = 0;
    const auto requesting_thread = std::this_thread::get_id();
    self = std::make_unique<callback_t>(source.get_token(), [&] {
        // Save everything needed before resetting the owning function object.
        auto* owner = &self;
        auto* counter = &calls;
        EXPECT_EQ(std::this_thread::get_id(), requesting_thread);
        owner->reset();
        ++*counter;
    });
    EXPECT_TRUE(source.request_stop());
    EXPECT_EQ(self, nullptr);
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(source.request_stop());
}

TEST(StopIntrusiveTest, RemovesPendingSiblingsAtDifferentListPositions) {
    std::inplace_stop_source source;
    using callback_t = std::inplace_stop_callback<std::function<void()>>;
    int surviving_calls = 0;
    int removed_calls = 0;
    auto oldest = std::make_unique<callback_t>(source.get_token(), [&] { ++removed_calls; });
    callback_t survivor(source.get_token(), [&] { ++surviving_calls; });
    auto middle = std::make_unique<callback_t>(source.get_token(), [&] { ++removed_calls; });
    callback_t remover(source.get_token(), [&] {
        oldest.reset();
        middle.reset();
    });
    EXPECT_TRUE(source.request_stop());
    EXPECT_EQ(removed_calls, 0);
    EXPECT_EQ(surviving_calls, 1);
}

TEST(StopIntrusiveTest, PendingDeregistrationDoesNotWaitForExecutingSibling) {
    std::inplace_stop_source source;
    std::binary_semaphore entered{0}, release{0};
    int pending_calls = 0;
    auto pending_function = [&] { ++pending_calls; };
    using pending_t = std::inplace_stop_callback<decltype(pending_function)>;
    auto pending = std::make_unique<pending_t>(source.get_token(), pending_function);
    std::inplace_stop_callback blocker(source.get_token(), [&] {
        entered.release();
        release.acquire();
    });
    std::thread requester([&] { EXPECT_TRUE(source.request_stop()); });
    entered.acquire();
    // This must return before releasing the unrelated invocation.
    pending.reset();
    EXPECT_EQ(pending_calls, 0);
    release.release();
    requester.join();
}

TEST(StopIntrusiveTest, CrossThreadDeregistrationWaitsForInvocationAndPublishesEffects) {
    std::inplace_stop_source source;
    std::binary_semaphore entered{0}, release{0}, destroying{0};
    bool invocation_finished = false;
    bool function_destroyed = false;
    // Construct from an lvalue whose destruction is not part of the assertion.
    blocking_function function{&entered, &release, &invocation_finished, &function_destroyed};
    using callback_t = std::inplace_stop_callback<blocking_function>;
    auto callback = std::make_unique<callback_t>(source.get_token(), function);
    function.function_destroyed = nullptr;
    std::atomic<bool> destruction_finished{false};
    std::thread requester([&] { EXPECT_TRUE(source.request_stop()); });
    entered.acquire();
    std::thread destroyer([&] {
#if defined(FORGE_EXECUTION_TEST_STOP_HOOKS) && !defined(__cpp_lib_inplace_stop_token)
        std::__forge_stop_detail::deregister_context = &destroying;
        std::__forge_stop_detail::deregister_wait_hook = [](void* context) noexcept {
            static_cast<std::binary_semaphore*>(context)->release();
        };
#else
        destroying.release();
#endif
        callback.reset();
        // This plain read must be ordered after the invocation's plain write.
        EXPECT_TRUE(invocation_finished);
        destruction_finished.store(true, std::memory_order_release);
    });
    destroying.acquire();
    EXPECT_FALSE(destruction_finished.load(std::memory_order_acquire));
    release.release();
    destroyer.join();
    requester.join();
    EXPECT_TRUE(function_destroyed);
    EXPECT_TRUE(destruction_finished.load(std::memory_order_acquire));
}

TEST(StopIntrusiveTest, RegistrationDuringInvocationRunsImmediatelyOnConstructingThread) {
    std::inplace_stop_source source;
    std::binary_semaphore entered{0}, release{0};
    int nested_calls = 0;
    std::inplace_stop_callback outer(source.get_token(), [&] {
        const auto current = std::this_thread::get_id();
        std::inplace_stop_callback nested(source.get_token(), [&] {
            EXPECT_EQ(std::this_thread::get_id(), current);
            ++nested_calls;
        });
        EXPECT_EQ(nested_calls, 1);
        EXPECT_FALSE(source.request_stop());
        entered.release();
        release.acquire();
    });
    std::thread requester([&] { EXPECT_TRUE(source.request_stop()); });
    entered.acquire();
    int concurrent_calls = 0;
    const auto constructing_thread = std::this_thread::get_id();
    std::inplace_stop_callback concurrent(source.get_token(), [&] {
        EXPECT_EQ(std::this_thread::get_id(), constructing_thread);
        ++concurrent_calls;
    });
    EXPECT_EQ(concurrent_calls, 1);
    release.release();
    requester.join();
    EXPECT_EQ(nested_calls, 1);
}

TEST(StopIntrusiveTest, ConcurrentRegisterRequestDestroyStress) {
    constexpr int rounds = 2000;
    constexpr int registrations = 12;
    // Explicit worker acknowledgements make every old-source access precede
    // reuse, including losing requests and callback notification handshakes.
    std::mutex mutex;
    std::condition_variable cv;
    int generation = 0;
    int finished = 0;
    std::inplace_stop_source* current = nullptr;
    std::atomic<int> winners{0};
    std::atomic<bool> duplicate{false};
    std::array<std::thread, 4> workers;
    for (int worker = 0; worker != 4; ++worker) {
        workers[worker] = std::thread([&, worker, rounds, registrations] {
            for (int round = 0; round != rounds; ++round) {
                {
                    std::unique_lock lock{mutex};
                    cv.wait(lock, [&] { return generation > round; });
                }
                if (worker < 2) {
                    for (int index = 0; index != registrations; ++index) {
                        std::atomic<unsigned> calls{0};
                        {
                            std::inplace_stop_callback callback(current->get_token(), [&] {
                                if (calls.fetch_add(1, std::memory_order_relaxed) != 0)
                                    duplicate.store(true, std::memory_order_relaxed);
                            });
                            if ((index + round + worker) % 3 == 0) std::this_thread::yield();
                        }
                    }
                } else if (current->request_stop()) {
                    winners.fetch_add(1, std::memory_order_relaxed);
                }
                {
                    std::lock_guard lock{mutex};
                    ++finished;
                }
                cv.notify_all();
            }
        });
    }
    for (int round = 0; round != rounds; ++round) {
        std::inplace_stop_source source;
        current = &source;
        int witness_calls = 0;
        std::inplace_stop_callback witness(source.get_token(), [&] { ++witness_calls; });
        winners.store(0, std::memory_order_relaxed);
        {
            std::lock_guard lock{mutex};
            finished = 0;
            generation = round + 1;
        }
        cv.notify_all();
        {
            std::unique_lock lock{mutex};
            cv.wait(lock, [&] { return finished == 4; });
        }
        EXPECT_EQ(winners.load(std::memory_order_relaxed), 1);
        EXPECT_EQ(witness_calls, 1);
        EXPECT_TRUE(source.stop_requested());
    }
    for (auto& worker : workers) worker.join();
    EXPECT_FALSE(duplicate.load(std::memory_order_relaxed));
}

#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME) && !defined(__cpp_lib_inplace_stop_token)
TEST(StopIntrusiveDeathTest, DetectsDeregistrationAfterSourceDestruction) {
    for (bool stopped : {false, true}) {
        EXPECT_DEATH({
            std::optional<std::inplace_stop_source> source{std::in_place};
            std::optional<std::inplace_stop_callback<nothrow_function>> callback;
            callback.emplace(source->get_token(), nothrow_function{});
            if (stopped) source->request_stop();
            source.reset();
            callback.reset();
        }, "inplace stop source accessed after destruction");
    }
}
#endif
