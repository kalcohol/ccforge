#include "static_opstate_allocation_probe.hpp"

#if !defined(FORGE_TEST_STATIC_OPSTATE_TSAN)
#include <array>
#include <memory>
#include <optional>

namespace {
namespace ex = std::execution;

struct allocation_counts {
    int allocations = 0;
    int deallocations = 0;
    int attempts = 0;
    int fail_on = 0;
};

// malloc-backed, including over-aligned rebinds: this allocator never calls new.
template<class T>
struct counting_allocator {
    using value_type = T;
    allocation_counts* counts;
    explicit counting_allocator(allocation_counts* c) noexcept : counts(c) {}
    template<class U>
    counting_allocator(const counting_allocator<U>& a) noexcept : counts(a.counts) {}
    T* allocate(std::size_t n) {
        if (++counts->attempts == counts->fail_on) throw std::bad_alloc{};
        const auto alignment = (alignof(T) > alignof(void*) ? alignof(T) : alignof(void*));
        void* raw = std::malloc(n * sizeof(T) + alignment - 1 + sizeof(void*));
        if (!raw) throw std::bad_alloc{};
        auto addr = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
        addr = (addr + alignment - 1) & ~(alignment - 1);
        auto* p = reinterpret_cast<T*>(addr);
        reinterpret_cast<void**>(p)[-1] = raw;
        ++counts->allocations;
        return p;
    }
    void deallocate(T* p, std::size_t) noexcept {
        ++counts->deallocations;
        std::free(reinterpret_cast<void**>(p)[-1]);
    }
    template<class U>
    bool operator==(const counting_allocator<U>& a) const noexcept { return counts == a.counts; }
};

auto allocator_env(allocation_counts& counts) {
    return ex::prop(ex::get_allocator, counting_allocator<std::byte>{&counts});
}

struct observations {
    int connects = 0;
    int starts = 0;
    int live = 0;
    int values = 0;
    int stopped = 0;
    bool aligned = true;
    bool fail_connect = false;
    bool deferred = false;
    void* pending = nullptr;
    void (*finish)(void*) noexcept = nullptr;
};
struct connect_error {};

template<std::size_t Bytes, std::size_t Alignment>
struct padded_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    observations* observed;
    auto get_env() const noexcept -> forge::empty_env { return {}; }

    template<class R>
    struct alignas(Alignment) op : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        R receiver;
        observations* observed;
        std::array<std::byte, Bytes> padding;
        struct stop_fn {
            op* self;
            void operator()() const noexcept { self->complete_stopped(); }
        };
        using token_t = std::remove_cvref_t<decltype(
            ex::get_stop_token(ex::get_env(std::declval<const R&>())))>;
        std::optional<std::stop_callback_for_t<token_t, stop_fn>> callback;
        op(R r, observations* o) : receiver(std::move(r)), observed(o) { ++observed->live; }
        ~op() { --observed->live; }
        void complete_stopped() noexcept {
            callback.reset();
            ex::set_stopped(std::move(receiver));
        }
        void start() & noexcept {
            ++observed->starts;
            observed->aligned &= reinterpret_cast<std::uintptr_t>(this) % Alignment == 0;
            if (observed->deferred) {
                auto token = ex::get_stop_token(ex::get_env(receiver));
                if (token.stop_requested()) { complete_stopped(); return; }
                observed->pending = this;
                observed->finish = [](void* p) noexcept {
                    auto* self = static_cast<op*>(p);
                    self->callback.reset();
                    ex::set_value(std::move(self->receiver));
                };
                callback.emplace(token, stop_fn{this});
            } else {
                ex::set_value(std::move(receiver));
            }
        }
    };
    template<class R>
    auto connect(R r) const -> op<R> {
        ++observed->connects;
        if (observed->fail_connect) throw connect_error{};
        return op<R>{std::move(r), observed};
    }
};

using large_sender = padded_sender<8192, 64>;

template<class Token = std::never_stop_token>
struct receiver {
    using receiver_concept = ex::receiver_t;
    observations* observed;
    Token token{};
    void set_value() && noexcept { ++observed->values; }
    template<class E> void set_error(E&&) && noexcept { std::terminate(); }
    void set_stopped() && noexcept { ++observed->stopped; }
    auto get_env() const noexcept {
        return ex::make_env(ex::prop(ex::get_stop_token, token),
            ex::prop(ex::get_start_scheduler, ex::inline_scheduler{}));
    }
};

struct large_schedule_sender;
struct large_scheduler {
    using scheduler_concept = ex::scheduler_t;
    observations* observed;
    auto schedule() const noexcept -> large_schedule_sender;
    bool operator==(const large_scheduler&) const noexcept = default;
};
struct large_schedule_sender : large_sender {
    auto get_env() const noexcept {
        return ex::prop(ex::get_completion_scheduler<ex::set_value_t>, large_scheduler{observed});
    }
};
inline auto large_scheduler::schedule() const noexcept -> large_schedule_sender {
    return {{observed}};
}
struct large_join_receiver : receiver<> {
    auto get_env() const noexcept {
        return ex::prop(ex::get_start_scheduler, large_scheduler{observed});
    }
};

template<class Scope>
void join(Scope& scope) {
    observations out;
    auto op = ex::connect(scope.join(), receiver<>{&out});
    ex::start(op);
    EXPECT_EQ(out.values, 1);
}

template<class Scope, class Sender>
void check_associate() {
    Scope scope;
    observations out;
    const auto calls = allocation_probe::measure([&] {
        auto op = ex::connect(ex::associate(Sender{&out}, scope.get_token()), receiver<>{&out});
        ex::start(op);
    });
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(out.values, 1);
    EXPECT_EQ(out.live, 0);
    EXPECT_TRUE(out.aligned);
    EXPECT_EQ(scope.count(), 0u);
    join(scope);
}

template<class Scope>
void check_spawn(bool future, bool abandoned) {
    Scope scope;
    observations out;
    out.deferred = abandoned;
    allocation_counts counts;
    const auto per_registration = allocation_probe::allocations_per_registration();
    const auto calls = allocation_probe::measure([&] {
        if (future) {
            auto f = ex::spawn_future(large_sender{&out}, scope.get_token(), allocator_env(counts));
            if (!abandoned) {
                auto op = ex::connect(std::move(f), receiver<>{&out});
                ex::start(op);
            }
        } else {
            ex::spawn(large_sender{&out}, scope.get_token(), allocator_env(counts));
        }
    });
    const auto registrations = future
        ? (std::is_same_v<Scope, ex::counting_scope> ? 2u : 0u) + (abandoned ? 1u : 0u)
        : 0u;
    EXPECT_EQ(calls, registrations * per_registration);
    EXPECT_EQ(counts.allocations, future ? (abandoned ? 3 : 4) : 1);
    EXPECT_EQ(counts.deallocations, counts.allocations);
    EXPECT_EQ(out.live, 0);
    EXPECT_TRUE(out.aligned);
    EXPECT_EQ(scope.count(), 0u);
    if (future && !abandoned) EXPECT_EQ(out.values, 1);
    join(scope);
}

template<class S>
struct attributed_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = typename S::completion_signatures;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    S source;
    allocation_counts* counts;
    auto get_env() const noexcept { return allocator_env(*counts); }
    template<class R> auto connect(R r) && { return ex::connect(std::move(source), std::move(r)); }
};
} // namespace

TEST(StaticOpstateScope, PositiveControl) {
    EXPECT_EQ(allocation_probe::measure([] {
        void* p = ::operator new(8192, std::align_val_t{64});
        ::operator delete(p, std::align_val_t{64});
    }), 1u);
}

TEST(StaticOpstateScope, AssociateSmallLargeAndOveraligned) {
    check_associate<ex::simple_counting_scope, padded_sender<1, 8>>();
    check_associate<ex::simple_counting_scope, large_sender>();
    check_associate<ex::counting_scope, padded_sender<1, 8>>();
    check_associate<ex::counting_scope, large_sender>();
}

TEST(StaticOpstateScope, AssociateFusesBothTokensWithoutStateAllocation) {
    ex::counting_scope scope;
    std::inplace_stop_source downstream;
    observations out;
    const auto per_registration = allocation_probe::allocations_per_registration();
    auto calls = allocation_probe::measure([&] {
        auto op = ex::connect(ex::associate(large_sender{&out}, scope.get_token()),
            receiver<std::inplace_stop_token>{&out, downstream.get_token()});
        ex::start(op);
    });
    EXPECT_EQ(calls, 2 * per_registration);
    EXPECT_EQ(out.values, 1);
    EXPECT_EQ(out.live, 0);
    join(scope);
}

TEST(StaticOpstateScope, JoinStoresScheduleOperationWithoutAllocation) {
    ex::counting_scope scope;
    observations out;
    auto association = scope.get_token().try_associate();
    auto calls = allocation_probe::measure([&] {
        auto op = ex::connect(scope.join(), receiver<>{&out});
        ex::start(op);
        association = {};
    });
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(out.values, 1);
}

TEST(StaticOpstateScope, JoinKeepsLargeOveralignedScheduleOperationInPlace) {
    ex::counting_scope scope;
    observations out;
    auto association = scope.get_token().try_associate();
    auto calls = allocation_probe::measure([&] {
        auto op = ex::connect(scope.join(), large_join_receiver{{&out}});
        ex::start(op);
        association = {};
    });
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(out.connects, 1);
    EXPECT_EQ(out.starts, 1);
    EXPECT_EQ(out.values, 1);
    EXPECT_EQ(out.live, 0);
    EXPECT_TRUE(out.aligned);
}

TEST(StaticOpstateScope, SpawnUsesOnlySelectedAllocator) {
    check_spawn<ex::simple_counting_scope>(false, false);
    check_spawn<ex::counting_scope>(false, false);
}

TEST(StaticOpstateScope, FutureConsumedAndAbandonedUseOnlySelectedAllocator) {
    check_spawn<ex::simple_counting_scope>(true, false);
    check_spawn<ex::simple_counting_scope>(true, true);
    check_spawn<ex::counting_scope>(true, false);
    check_spawn<ex::counting_scope>(true, true);
}

TEST(StaticOpstateScope, SplitOneAndSeveralConsumersUseOnlySelectedAllocator) {
    for (bool several : {false, true}) {
        observations out;
        allocation_counts counts;
        const auto calls = allocation_probe::measure([&] {
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
        EXPECT_EQ(calls, 0u);
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
    auto calls = allocation_probe::measure([&] {
        auto shared = forge::split(attributed_sender<large_sender>{{&out}, &counts});
        auto first = ex::connect(shared, receiver<>{&out});
        auto second = ex::connect(shared, receiver<>{&out});
        auto third = ex::connect(shared, receiver<>{&out});
        ex::start(first);
        ex::start(second);
        ex::start(third);
        out.finish(out.pending);
    });
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(counts.allocations, 4);
    EXPECT_EQ(counts.deallocations, 4);
    EXPECT_EQ(out.starts, 1);
    EXPECT_EQ(out.values, 3);
    EXPECT_EQ(out.live, 0);
}

#endif
