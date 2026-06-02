#include <gtest/gtest.h>
#include <execution>
#include <atomic>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

struct counting_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::atomic<int>* completions = nullptr;

    void set_value() && noexcept {
        completions->fetch_add(1, std::memory_order_acq_rel);
    }

    template<class E>
    void set_error(E&&) && noexcept {}

    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

template<class Scope>
struct chained_join_receiver {
    using receiver_concept = std::execution::receiver_t;

    Scope* scope = nullptr;
    std::atomic<int>* completions = nullptr;

    void set_value() && noexcept {
        completions->fetch_add(1, std::memory_order_acq_rel);
        auto result = std::execution::sync_wait(scope->join());
        if (result.has_value()) {
            completions->fetch_add(1, std::memory_order_acq_rel);
        }
    }

    template<class E>
    void set_error(E&&) && noexcept {}

    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

void spin_until_go(std::atomic<int>& ready, std::atomic<bool>& go) noexcept {
    ready.fetch_add(1, std::memory_order_acq_rel);
    while (!go.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void perturb(int iteration, int lane) noexcept {
    const int yields = (iteration * 17 + lane * 5) % 4;
    for (int i = 0; i < yields; ++i) {
        std::this_thread::yield();
    }
}

template<class Scope>
void stress_join_start_races_last_release() {
    constexpr int kIterations = 512;

    for (int i = 0; i < kIterations; ++i) {
        Scope scope;
        auto token = scope.get_token();
        auto assoc = token.try_associate();
        ASSERT_TRUE(static_cast<bool>(assoc));

        std::atomic<int> completions{0};
        auto op = std::execution::connect(
            scope.join(),
            counting_receiver{&completions});

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        std::thread joiner{[&] {
            spin_until_go(ready, go);
            perturb(i, 0);
            std::execution::start(op);
        }};
        std::thread releaser{[&] {
            spin_until_go(ready, go);
            perturb(i, 1);
            assoc = decltype(assoc){};
        }};

        while (ready.load(std::memory_order_acquire) != 2) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        joiner.join();
        releaser.join();

        EXPECT_EQ(completions.load(std::memory_order_acquire), 1);
        EXPECT_EQ(scope.count(), 0u);
    }
}

template<class Scope>
void stress_multiple_joiners_race_last_release() {
    constexpr int kIterations = 256;

    for (int i = 0; i < kIterations; ++i) {
        Scope scope;
        auto token = scope.get_token();
        auto assoc = token.try_associate();
        ASSERT_TRUE(static_cast<bool>(assoc));

        std::atomic<int> completions{0};
        auto first = std::execution::connect(scope.join(), counting_receiver{&completions});
        auto second = std::execution::connect(scope.join(), counting_receiver{&completions});
        auto third = std::execution::connect(scope.join(), counting_receiver{&completions});

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};

        std::thread a{[&] {
            spin_until_go(ready, go);
            perturb(i, 0);
            std::execution::start(first);
        }};
        std::thread b{[&] {
            spin_until_go(ready, go);
            perturb(i, 1);
            std::execution::start(second);
        }};
        std::thread c{[&] {
            spin_until_go(ready, go);
            perturb(i, 2);
            std::execution::start(third);
        }};
        std::thread releaser{[&] {
            spin_until_go(ready, go);
            perturb(i, 3);
            assoc = decltype(assoc){};
        }};

        while (ready.load(std::memory_order_acquire) != 4) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        a.join();
        b.join();
        c.join();
        releaser.join();

        EXPECT_EQ(completions.load(std::memory_order_acquire), 3);
        EXPECT_EQ(scope.count(), 0u);
    }
}

template<class Scope>
void expect_join_completion_can_start_another_join() {
    Scope scope;
    auto token = scope.get_token();
    auto assoc = token.try_associate();
    ASSERT_TRUE(static_cast<bool>(assoc));

    std::atomic<int> completions{0};
    auto op = std::execution::connect(
        scope.join(),
        chained_join_receiver<Scope>{&scope, &completions});

    std::execution::start(op);
    assoc = decltype(assoc){};

    EXPECT_EQ(completions.load(std::memory_order_acquire), 2);
    EXPECT_EQ(scope.count(), 0u);
}

} // namespace

TEST(SimpleCountingScopeStressTest, JoinStartRacesLastRelease) {
    stress_join_start_races_last_release<std::execution::simple_counting_scope>();
}

TEST(CountingScopeStressTest, JoinStartRacesLastRelease) {
    stress_join_start_races_last_release<std::execution::counting_scope>();
}

TEST(SimpleCountingScopeStressTest, MultipleJoinersRaceLastRelease) {
    stress_multiple_joiners_race_last_release<std::execution::simple_counting_scope>();
}

TEST(CountingScopeStressTest, MultipleJoinersRaceLastRelease) {
    stress_multiple_joiners_race_last_release<std::execution::counting_scope>();
}

TEST(SimpleCountingScopeStressTest, JoinCompletionCanStartAnotherJoin) {
    expect_join_completion_can_start_another_join<std::execution::simple_counting_scope>();
}

TEST(CountingScopeStressTest, JoinCompletionCanStartAnotherJoin) {
    expect_join_completion_can_start_another_join<std::execution::counting_scope>();
}

