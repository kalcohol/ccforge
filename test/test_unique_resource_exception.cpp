#include <gtest/gtest.h>

#include <stdexcept>

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
    constructor_throwing_deleter(const constructor_throwing_deleter&) = delete;
    constructor_throwing_deleter& operator=(const constructor_throwing_deleter&) = delete;

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
        auto resource = std::make_unique_resource(acquire_resource(), constructor_throwing_deleter{});
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
