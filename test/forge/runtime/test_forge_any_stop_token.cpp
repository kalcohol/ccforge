#include <gtest/gtest.h>

#include <forge/any_stop_token.hpp>

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace {

struct stop_callback_probe {
    void operator()() noexcept {}
};

struct qualified_stop_callback {
    int* lvalue_calls;
    int* rvalue_calls;

    void operator()() & noexcept { ++*lvalue_calls; }
    void operator()() && noexcept { ++*rvalue_calls; }
};

struct rvalue_stop_callback {
    int* calls;
    void operator()() && noexcept { ++*calls; }
};

struct non_stopping_state_token {
    const void* identity = nullptr;

    template<class Callback>
    struct callback_type {
        callback_type(non_stopping_state_token, Callback) noexcept {}
    };

    bool stop_requested() const noexcept { return false; }
    bool stop_possible() const noexcept { return false; }
    bool operator==(const non_stopping_state_token&) const noexcept = default;
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

TEST(ForgeAnyStopTokenTest, AllUnassociatedRepresentationsCompareEqual) {
    const std::array tokens{
        forge::any_stop_token{},
        forge::any_stop_token{std::inplace_stop_token{}},
        forge::any_stop_token{std::never_stop_token{}},
    };
    for (const auto& left : tokens) {
        EXPECT_FALSE(left.stop_possible());
        for (const auto& right : tokens) {
            EXPECT_EQ(left, right);
        }
    }
}

TEST(ForgeAnyStopTokenTest, NonStoppingStatesRetainTheirDistinctIdentity) {
    int first_state = 0;
    int second_state = 0;
    non_stopping_state_token first{&first_state};
    non_stopping_state_token second{&second_state};
    forge::any_stop_token erased_first{first};
    forge::any_stop_token erased_same{first};
    forge::any_stop_token erased_second{second};

    EXPECT_FALSE(erased_first.stop_possible());
    EXPECT_FALSE(erased_second.stop_possible());
    EXPECT_EQ(erased_first, erased_same);
    EXPECT_NE(erased_first, erased_second);
    EXPECT_NE(erased_first, forge::any_stop_token{});
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

TEST(ForgeAnyStopTokenTest, EqualityTracksTheLogicalStopState) {
    std::inplace_stop_source first_source;
    std::inplace_stop_source second_source;
    forge::any_stop_token first{first_source.get_token()};
    forge::any_stop_token same_state{first_source.get_token()};
    forge::any_stop_token other_state{second_source.get_token()};
    forge::any_stop_token disengaged;

    EXPECT_EQ(first, same_state);
    EXPECT_NE(first, other_state);
    EXPECT_NE(first, disengaged);
    EXPECT_EQ(disengaged, forge::any_stop_token{});

    EXPECT_TRUE(first_source.request_stop());
    EXPECT_EQ(first, same_state);
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

TEST(ForgeAnyStopTokenTest, CallbackInvocationPreservesCallbackTypeCategory) {
    for (bool already_stopped : {false, true}) {
        std::inplace_stop_source source;
        if (already_stopped) {
            source.request_stop();
        }
        forge::any_stop_token token{source.get_token()};
        int lvalue_calls = 0;
        int rvalue_calls = 0;
        int rvalue_only_calls = 0;
        qualified_stop_callback function{&lvalue_calls, &rvalue_calls};
        forge::any_stop_token::callback_type<qualified_stop_callback> owned(
            token, function);
        forge::any_stop_token::callback_type<qualified_stop_callback&> borrowed(
            token, function);
        forge::any_stop_token::callback_type<rvalue_stop_callback> rvalue_only(
            token, rvalue_stop_callback{&rvalue_only_calls});

        EXPECT_EQ(source.request_stop(), !already_stopped);
        EXPECT_EQ(lvalue_calls, 1);
        EXPECT_EQ(rvalue_calls, 1);
        EXPECT_EQ(rvalue_only_calls, 1);
    }
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
