#include <gtest/gtest.h>

#include <execution>

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <utility>

namespace {

struct join_observer {
    using receiver_concept = std::execution::receiver_t;

    std::atomic<int>* completion;

    void set_value() && noexcept { completion->store(1, std::memory_order_release); }
    template<class E>
    void set_error(E&&) && noexcept { completion->store(2, std::memory_order_release); }
    void set_stopped() && noexcept { completion->store(3, std::memory_order_release); }

    auto get_env() const noexcept {
        return std::execution::make_env(std::execution::make_prop(
            std::execution::get_start_scheduler_t{},
            std::execution::inline_scheduler{}));
    }
};

template<class Scope>
void expect_destruction_synchronizes_with_decrement_tail() {
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "This regression requires a lock-free count observer");
    for (int iteration = 0; iteration < 4096; ++iteration) {
        SCOPED_TRACE(iteration);
        auto scope = std::make_unique<Scope>();
        auto association = scope->get_token().try_associate();
        ASSERT_TRUE(static_cast<bool>(association));

        std::atomic<int> completion{0};
        // This operation and its receiver outlive the scope and the releaser.
        // Starting while nonempty registers a join before the final decrement.
        auto join_op = std::execution::connect(scope->join(), join_observer{&completion});
        std::execution::start(join_op);
        EXPECT_EQ(completion.load(std::memory_order_acquire), 0);

        std::atomic<bool> go{false};
        std::thread releaser{[association = std::move(association), &go]() mutable {
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            association = decltype(association){};
        }};
        go.store(true, std::memory_order_release);

        // count() reaches zero before __decrement publishes joined, detaches
        // the joiner, and unlocks. Do not observe join completion or join the
        // thread until after destruction: that would hide the destructor race.
        while (scope->count() != 0) {}
        scope.reset();

        releaser.join();
        EXPECT_EQ(completion.load(std::memory_order_acquire), 1);
    }
}

} // namespace

TEST(SimpleCountingScopeDecrementTailTest, DestructionSynchronizesWithFinalDecrement) {
    expect_destruction_synchronizes_with_decrement_tail<
        std::execution::simple_counting_scope>();
}

TEST(CountingScopeDecrementTailTest, DestructionSynchronizesWithFinalDecrement) {
    expect_destruction_synchronizes_with_decrement_tail<std::execution::counting_scope>();
}
