#include <execution>

#include <gtest/gtest.h>

#include <array>
#include <thread>
#include <utility>

namespace {

namespace ex = std::execution;

struct receiver {
    using receiver_concept = ex::receiver_tag;
    unsigned* values;
    void set_value() && noexcept { ++*values; }
};

TEST(ExecutionTaskScheduler, E06_NativeParUsesDefaultSerialBulk) {
    for (int shape : {0, 3}) {
        SCOPED_TRACE(shape);
        ex::task_scheduler scheduler{ex::inline_scheduler{}};
        unsigned values = 0;
        unsigned calls = 0;
        std::array<unsigned, 3> visits{};
        std::array<int, 3> order{};
        const auto caller = std::this_thread::get_id();
        auto sender = ex::bulk(ex::schedule(scheduler), ex::par, shape, [&](int index) noexcept {
            EXPECT_EQ(std::this_thread::get_id(), caller);
            if (std::this_thread::get_id() != caller) return;
            EXPECT_GE(index, 0);
            EXPECT_LT(index, shape);
            if (index >= 0 && index < static_cast<int>(visits.size())) ++visits[index];
            if (calls < order.size()) order[calls] = index;
            ++calls;
        });
        auto op = ex::connect(std::move(sender), receiver{&values});
        ex::start(op);
        EXPECT_EQ(values, 1u);
        EXPECT_EQ(calls, static_cast<unsigned>(shape));
        EXPECT_EQ(visits, (shape == 0 ? std::array{0u, 0u, 0u} : std::array{1u, 1u, 1u}));
        if (shape) EXPECT_EQ(order, (std::array{0, 1, 2}));
    }
}

} // namespace
