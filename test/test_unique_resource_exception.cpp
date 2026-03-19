#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "../backport/cpp26/unique_resource.hpp"

namespace {

int active_resources = 0;
int next_resource_id = 0;

int acquire_resource() {
    ++active_resources;
    return ++next_resource_id;
}

struct transfer_handle {
    int value;

    explicit transfer_handle(int initial_value) noexcept
        : value(initial_value) {}

    transfer_handle(const transfer_handle&) = delete;
    transfer_handle& operator=(const transfer_handle&) = delete;

    transfer_handle(transfer_handle&& other) noexcept
        : value(other.value) {
        other.value = 0;
    }

    transfer_handle& operator=(transfer_handle&& other) noexcept {
        value = other.value;
        other.value = 0;
        return *this;
    }
};

transfer_handle acquire_transfer_handle() {
    ++active_resources;
    return transfer_handle(++next_resource_id);
}

void reset_resource_counters() {
    active_resources = 0;
    next_resource_id = 0;
}

struct constructor_throwing_deleter {
    constructor_throwing_deleter() = default;
    constructor_throwing_deleter& operator=(const constructor_throwing_deleter&) = delete;

    constructor_throwing_deleter(const constructor_throwing_deleter&) {
        throw std::runtime_error("deleter copy construction failed");
    }

    constructor_throwing_deleter(constructor_throwing_deleter&&) {
        throw std::runtime_error("deleter construction failed");
    }

    void operator()(int resource_id) const noexcept {
        if (resource_id != 0) {
            --active_resources;
        }
    }
};

struct move_only_throwing_deleter {
    struct token {};

    move_only_throwing_deleter() = delete;
    explicit move_only_throwing_deleter(token) noexcept {}
    move_only_throwing_deleter(const move_only_throwing_deleter&) = delete;
    move_only_throwing_deleter& operator=(const move_only_throwing_deleter&) = delete;

    move_only_throwing_deleter(move_only_throwing_deleter&&) {
        throw std::runtime_error("deleter move failed");
    }

    move_only_throwing_deleter& operator=(move_only_throwing_deleter&&) = delete;

    void operator()(const transfer_handle& resource) const noexcept {
        if (resource.value != 0) {
            --active_resources;
        }
    }
};

} // namespace

TEST(UniqueResourceExceptionTest, ConstructorFailureCleansUpResource) {
    reset_resource_counters();

    try {
        auto resource = std::unique_resource(acquire_resource(), constructor_throwing_deleter{});
        (void)resource;
        FAIL() << "expected constructor_throwing_deleter to throw";
    } catch (const std::runtime_error&) {
    }

    EXPECT_EQ(active_resources, 0);
}

TEST(UniqueResourceExceptionTest, MoveFailureCleansUpTransferredResource) {
    reset_resource_counters();

    try {
        std::unique_resource<transfer_handle, move_only_throwing_deleter> original(
            acquire_transfer_handle(),
            move_only_throwing_deleter::token{});
        auto moved = std::move(original);
        (void)moved;
        FAIL() << "expected move_only_throwing_deleter to throw";
    } catch (const std::runtime_error&) {
    }

    EXPECT_EQ(active_resources, 0);
}

// T-10: reset(R) exception safety — if resource assignment throws,
// the new resource is cleaned up via the deleter
namespace {

struct throwing_on_assign_resource {
    int value;

    explicit throwing_on_assign_resource(int v) noexcept : value(v) {}

    throwing_on_assign_resource(const throwing_on_assign_resource& other) noexcept
        : value(other.value) {}

    throwing_on_assign_resource(throwing_on_assign_resource&& other) noexcept
        : value(other.value) {
        other.value = 0;
    }

    throwing_on_assign_resource& operator=(const throwing_on_assign_resource&) {
        throw std::runtime_error("assignment throws");
    }

    throwing_on_assign_resource& operator=(throwing_on_assign_resource&&) {
        throw std::runtime_error("move assignment throws");
    }
};

bool operator==(const throwing_on_assign_resource& lhs,
                const throwing_on_assign_resource& rhs) noexcept {
    return lhs.value == rhs.value;
}

struct throwing_resource_deleter {
    int* calls;
    std::vector<int>* cleaned;

    void operator()(const throwing_on_assign_resource& r) const noexcept {
        ++*calls;
        if (cleaned) cleaned->push_back(r.value);
    }
};

} // namespace

TEST(UniqueResourceExceptionTest, ResetWithValueCallsDeleterOnNewResourceWhenAssignmentThrows) {
    int cleanup_count = 0;
    std::vector<int> cleaned_values;

    std::unique_resource resource(
        throwing_on_assign_resource{1},
        throwing_resource_deleter{&cleanup_count, &cleaned_values});

    // reset(r) should try to assign r into the stored resource.
    // When assignment throws, the standard requires d(r) to be called.
    EXPECT_THROW(resource.reset(throwing_on_assign_resource{99}), std::runtime_error);

    // The new resource (99) should have been cleaned up by the deleter
    EXPECT_TRUE(std::find(cleaned_values.begin(), cleaned_values.end(), 99) != cleaned_values.end());
}
