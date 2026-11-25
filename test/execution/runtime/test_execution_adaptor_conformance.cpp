#include "test_bulk.hpp"
#include <gtest/gtest.h>
#include <execution>

#include <exception>
#include <memory_resource>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {
namespace ex = std::execution;
struct result { int values = 0; int errors = 0; std::exception_ptr error; };
struct receiver {
    using receiver_concept = ex::receiver_tag;
    result* out;
    ex::run_loop* loop = nullptr;
    template<class... As> void set_value(As&&...) && noexcept {
        ++out->values;
        if (loop) loop->finish();
    }
    void set_error(std::exception_ptr e) && noexcept {
        ++out->errors;
        out->error = std::move(e);
        if (loop) loop->finish();
    }
    void set_stopped() && noexcept { if (loop) loop->finish(); }
    auto get_env() const noexcept { return ex::prop{ex::get_start_scheduler, ex::inline_scheduler{}}; }
};
struct connecting_sender {
    using sender_concept = ex::sender_tag;
    int* connects;
    std::thread::id* agent;
    bool fail;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<ex::set_value_t()> { return {}; }
    template<class R>
    auto connect(R r) const -> ex::connect_result_t<decltype(ex::just()), R> {
        ++*connects;
        *agent = std::this_thread::get_id();
        if (fail) throw std::runtime_error("late child connect");
        return ex::connect(ex::just(), std::move(r));
    }
};
TEST(ExecutionAdaptorConformance, StartsOnConnectsChildOnTargetAgent) {
    for (bool fail : {false, true}) {
        ex::run_loop loop;
        result out;
        int connects = 0;
        std::thread::id agent;
        auto op = ex::connect(ex::starts_on(loop.get_scheduler(),
            connecting_sender{&connects, &agent, fail}), receiver{&out, &loop});
        EXPECT_EQ(connects, 0);
        std::thread worker([&] { loop.run(); });
        const auto target = worker.get_id();
        ex::start(op);
        worker.join();
        EXPECT_EQ(connects, 1);
        EXPECT_EQ(agent, target);
        EXPECT_EQ(out.values, fail ? 0 : 1);
        EXPECT_EQ(out.errors, fail ? 1 : 0);
        if (fail) {
            ASSERT_TRUE(out.error);
            EXPECT_THROW(std::rethrow_exception(out.error), std::runtime_error);
        }
    }
}
TEST(ExecutionAdaptorConformance, AssociatedPredecessorReleasesBeforeJoin) {
    ex::simple_counting_scope scope;
    result out;
    auto sender = ex::let_value(ex::associate(ex::just(), scope.get_token()), [&] {
        return scope.join();
    });
    auto op = ex::connect(std::move(sender), receiver{&out});
    ex::start(op);
    EXPECT_EQ(out.values, 1);
    EXPECT_EQ(out.errors, 0);
}
TEST(ExecutionAdaptorConformance, LockedPredecessorsCanBeReplacedDuringCompletion) {
    const auto check = [](auto source) {
        auto sender = ex::let_value(std::move(source), [](auto&...) noexcept { return ex::just(42); });
        auto value = std::this_thread::sync_wait(std::move(sender));
        ASSERT_TRUE(value);
        EXPECT_EQ(std::get<0>(*value), 42);
    };
    check(ex::when_all(ex::just(1), ex::just(2)));
    ex::simple_counting_scope scope;
    check(ex::spawn_future(ex::just(1), scope.get_token()));
    auto joined = std::this_thread::sync_wait(scope.join());
    ASSERT_TRUE(joined);
    ex::run_loop loop;
    result loop_result;
    auto op = ex::connect(ex::let_value(ex::schedule(loop.get_scheduler()), [&]() noexcept {
        loop.finish();
        return ex::just();
    }), receiver{&loop_result});
    ex::start(op);
    loop.run();
    EXPECT_EQ(loop_result.values, 1);
}
class counting_resource : public std::pmr::memory_resource {
public:
    int allocations = 0;
private:
    void* do_allocate(std::size_t size, std::size_t align) override {
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(size, align);
    }
    void do_deallocate(void* p, std::size_t size, std::size_t align) override {
        std::pmr::new_delete_resource()->deallocate(p, size, align);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};
struct allocated_env {
    std::pmr::memory_resource* memory;
    auto query(std::get_allocator_t) const noexcept {
        return std::pmr::polymorphic_allocator<std::byte>{memory};
    }
};
struct allocation_result { std::pmr::memory_resource* resource = nullptr; int values = 0; };
struct allocated_receiver {
    using receiver_concept = ex::receiver_tag;
    allocated_env env;
    allocation_result* out;
    void set_value(std::pmr::vector<int>&& v) && noexcept {
        out->resource = v.get_allocator().resource();
        out->values = static_cast<int>(v.size());
    }
    void set_value(bool value) && noexcept { out->values = value ? 1 : 0; }
    void set_value(int value) && noexcept { out->values = value; }
    template<class E> void set_error(E&&) && noexcept { out->values = -1; }
    void set_stopped() && noexcept { out->values = -2; }
    auto get_env() const noexcept -> allocated_env { return env; }
};
struct allocated_function {
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;
    std::pmr::memory_resource* memory = nullptr;
    std::pmr::memory_resource** observed = nullptr;
    allocated_function() = default;
    explicit allocated_function(std::pmr::memory_resource** out) : observed(out) {}
    allocated_function(std::allocator_arg_t, allocator_type a, allocated_function other)
        : memory(a.resource()), observed(other.observed) {}
    int operator()(int value) const noexcept {
        *observed = memory;
        return value;
    }
    void operator()(int, int& value) const noexcept {
        *observed = memory;
        ++value;
    }
};
TEST(ExecutionAdaptorConformance, ReceiverAllocatorReconstructsValueAndErrorData) {
    counting_resource memory;
    allocation_result out;
    auto op = ex::connect(ex::just(std::pmr::vector<int>{1, 2, 3}),
        allocated_receiver{{&memory}, &out});
    EXPECT_GT(memory.allocations, 0);
    ex::start(op);
    EXPECT_EQ(out.resource, &memory);
    EXPECT_EQ(out.values, 3);
    struct error_receiver : allocated_receiver {
        void set_error(std::pmr::vector<int>&& v) && noexcept {
            std::move(static_cast<allocated_receiver&>(*this)).set_value(std::move(v));
        }
    };
    auto error = ex::connect(ex::just_error(std::pmr::vector<int>{1, 2}),
        error_receiver{{{&memory}, &out}});
    ex::start(error);
    EXPECT_EQ(out.resource, &memory);
    EXPECT_EQ(out.values, 2);
}
TEST(ExecutionAdaptorConformance, ReceiverAllocatorReconstructsThenAndBulkFunctions) {
    counting_resource memory;
    std::pmr::memory_resource* observed = nullptr;
    allocation_result out;
    auto op = ex::connect(ex::then(ex::just(7), allocated_function{&observed}),
        allocated_receiver{{&memory}, &out});
    ex::start(op);
    EXPECT_EQ(observed, &memory);
    EXPECT_EQ(out.values, 7);
    auto bulk = ex::connect(test_bulk::bulk_unchunked(ex::just(0), 3, allocated_function{&observed}),
        allocated_receiver{{&memory}, &out});
    ex::start(bulk);
    EXPECT_EQ(observed, &memory);
    EXPECT_EQ(out.values, 3);
}
struct rejecting_allocated_function {
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;
    rejecting_allocated_function() = default;
    rejecting_allocated_function(std::allocator_arg_t, allocator_type, rejecting_allocated_function) {
        throw std::runtime_error("allocator-aware state construction");
    }
    int operator()(int value) const noexcept { return value; }
};
// Reconstruction may allocate or throw, but default get-state is noexcept.
// No terminating reconstruction is evaluated by this test.
using rejecting_sender = decltype(ex::then(ex::just(1), rejecting_allocated_function{}));
static_assert(noexcept(ex::connect(std::declval<rejecting_sender>(),
    std::declval<allocated_receiver>())));
static_assert(noexcept(ex::connect(std::declval<rejecting_sender>(),
    std::declval<receiver>())));
TEST(ExecutionAdaptorConformance, MissingAllocatorMovesDataOnceWithoutCopies) {
    struct data {
        int* moves;
        int* copies;
        data(int* m, int* c) : moves(m), copies(c) {}
        data(const data& d) : moves(d.moves), copies(d.copies) { ++*copies; }
        data(data&& d) noexcept : moves(d.moves), copies(d.copies) { ++*moves; }
    };
    int moves = 0;
    int copies = 0;
    auto sender = ex::just(data{&moves, &copies});
    moves = copies = 0;
    result out;
    auto op = ex::connect(std::move(sender), receiver{&out});
    EXPECT_EQ(moves, 1);
    EXPECT_EQ(copies, 0);
    ex::start(op);
    EXPECT_EQ(out.values, 1);
}
struct nested_copy_data {
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;
    int* copies;
    int* reconstructions;
    bool* reject_copy;
    nested_copy_data(int* c, int* a, bool* reject) : copies(c), reconstructions(a), reject_copy(reject) {}
    nested_copy_data(nested_copy_data&&) noexcept = default;
    nested_copy_data(const nested_copy_data& other)
        : copies(other.copies), reconstructions(other.reconstructions), reject_copy(other.reject_copy) {
        ++*copies;
        if (*reject_copy) throw std::runtime_error("ordinary child copy");
    }
    nested_copy_data(std::allocator_arg_t, allocator_type, const nested_copy_data& other) noexcept
        : copies(other.copies), reconstructions(other.reconstructions), reject_copy(other.reject_copy) {
        ++*reconstructions;
    }
    nested_copy_data(std::allocator_arg_t, allocator_type a, nested_copy_data&& other) noexcept
        : nested_copy_data(std::allocator_arg, a, std::as_const(other)) {}
};
struct ignore_arguments { template<class... As> void operator()(As&&...) const noexcept {} };
struct nested_allocated_receiver : receiver {
    counting_resource* memory;
    auto get_env() const noexcept -> allocated_env { return {memory}; }
};
TEST(ExecutionAdaptorConformance, NestedLvalueChildUsesAllocatorBeforeAnyOrdinaryCopy) {
    int copies = 0;
    int reconstructions = 0;
    bool reject = false;
    counting_resource memory;
    const auto check = [&](auto sender) {
        copies = reconstructions = 0;
        reject = true;
        result out;
        EXPECT_THROW((void)ex::connect(std::as_const(sender), receiver{&out}), std::runtime_error);
        EXPECT_EQ(copies, 1);
        EXPECT_EQ(reconstructions, 0);
        copies = 0;
        auto op = ex::connect(std::as_const(sender), nested_allocated_receiver{{&out}, &memory});
        EXPECT_EQ(copies, 0);
        EXPECT_EQ(reconstructions, 1);
        ex::start(op);
        EXPECT_EQ(out.values, 1);
        EXPECT_EQ(out.errors, 0);
        reject = false;
    };
    const auto child = [&] { return ex::just(nested_copy_data{&copies, &reconstructions, &reject}); };
    check(ex::then(child(), ignore_arguments{}));
    check(ex::upon_error(ex::just_error(nested_copy_data{&copies, &reconstructions, &reject}), ignore_arguments{}));
    check(ex::upon_stopped(child(), ignore_arguments{}));
    check(test_bulk::bulk_unchunked(child(), 1, ignore_arguments{}));
    check(test_bulk::bulk_chunked(child(), 1, ignore_arguments{}));
    check(ex::into_variant(child()));
    check(ex::write_env(child(), ex::env<>{}));
    check(ex::schedule_from(child()));
    // Multiple wrappers must preserve the same const-lvalue child path.
    check(ex::then(ex::into_variant(ex::write_env(child(), ex::env<>{})), ignore_arguments{}));
}
} // namespace
