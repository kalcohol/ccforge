#include "static_opstate_sanitizer.hpp"
#include "static_opstate_scope_fixture.hpp"
#include <forge/split.hpp>
#include <gtest/gtest.h>

#if !defined(FORGE_TEST_STATIC_OPSTATE_TSAN)
#include "global_allocation_probe.hpp"
#endif

namespace {
namespace ex = std::execution;
using namespace static_opstate_scope_fixture;

// TSan owns the replacement operators. Run the same allocator, delivery and
// teardown checks there without measuring the process-wide allocation count.
template<class F>
void exercise_split(F&& run) {
#if defined(FORGE_TEST_STATIC_OPSTATE_TSAN)
    std::forward<F>(run)();
#else
    // Calibrate this executable's counter before checking for zero allocations.
    EXPECT_EQ(allocation_probe::measure([] {
        void* p = ::operator new(8192, std::align_val_t{64});
        ::operator delete(p, std::align_val_t{64});
    }), 1u);
    const auto calls = allocation_probe::measure(std::forward<F>(run));
    EXPECT_EQ(calls, 0u);
#endif
}

template<class S>
struct attributed_sender {
    using sender_concept = ex::sender_tag;
    using completion_signatures = typename S::completion_signatures;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    S source;
    allocation_counts* counts;
    auto get_env() const noexcept { return allocator_env(*counts); }
    template<class R> auto connect(R r) && { return ex::connect(std::move(source), std::move(r)); }
};

TEST(StaticOpstateScope, SplitOneAndSeveralConsumersUseOnlySelectedAllocator) {
    for (bool several : {false, true}) {
        observations out;
        allocation_counts counts;
        exercise_split([&] {
            auto shared = forge::split(attributed_sender<large_sender>{{&out}, &counts});
            auto first = ex::connect(shared, receiver<>{&out});
            ex::start(first);
            if (several) {
                auto second = ex::connect(shared, receiver<>{&out});
                auto third = ex::connect(shared, receiver<>{&out});
                ex::start(second);
                ex::start(third);
            }
        });
        EXPECT_EQ(counts.allocations, several ? 4 : 2);
        EXPECT_EQ(counts.deallocations, counts.allocations);
        EXPECT_EQ(out.starts, 1);
        EXPECT_EQ(out.values, several ? 3 : 1);
        EXPECT_EQ(out.live, 0);
        EXPECT_TRUE(out.aligned);
    }
}
TEST(StaticOpstateScope, SplitQueuesSeveralConsumersWithoutRegistrationAllocation) {
    observations out;
    out.deferred = true;
    allocation_counts counts;
    exercise_split([&] {
        auto shared = forge::split(attributed_sender<large_sender>{{&out}, &counts});
        auto first = ex::connect(shared, receiver<>{&out});
        auto second = ex::connect(shared, receiver<>{&out});
        auto third = ex::connect(shared, receiver<>{&out});
        ex::start(first);
        ex::start(second);
        ex::start(third);
        out.finish(out.pending);
    });
    EXPECT_EQ(counts.allocations, 4);
    EXPECT_EQ(counts.deallocations, 4);
    EXPECT_EQ(out.starts, 1);
    EXPECT_EQ(out.values, 3);
    EXPECT_EQ(out.live, 0);
}
} // namespace
