#include <gtest/gtest.h>

#include <string>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <memory>

namespace {

struct counting_deleter {
    int* calls;

    void operator()(int) const noexcept {
        ++*calls;
    }
};

struct recording_deleter {
    std::vector<int>* cleaned;

    void operator()(int v) const noexcept {
        cleaned->push_back(v);
    }
};

struct simple_object {
    int x;
    int y;
};

struct pointer_deleter {
    int* calls;

    void operator()(simple_object* p) const noexcept {
        ++*calls;
        delete p;
    }
};

struct static_counting_deleter {
    static int calls;

    void operator()(int) const noexcept {
        ++calls;
    }
};

int static_counting_deleter::calls = 0;

template<bool NothrowSwap>
struct swap_tracked_handle {
    int value = 0;
    int* swaps = nullptr;

    friend void swap(swap_tracked_handle& lhs, swap_tracked_handle& rhs) noexcept(NothrowSwap) {
        if constexpr (!NothrowSwap) {
            throw std::runtime_error("resource swap must not be called");
        } else {
            if (auto* count = lhs.swaps ? lhs.swaps : rhs.swaps) {
                ++*count;
            }
            std::swap(lhs.value, rhs.value);
            std::swap(lhs.swaps, rhs.swaps);
        }
    }
};

template<bool NothrowSwap>
struct swap_tracked_deleter {
    int owner = 0;
    std::vector<std::pair<int, int>>* cleaned = nullptr;
    int* swaps = nullptr;

    template<bool ResourceNothrowSwap>
    void operator()(const swap_tracked_handle<ResourceNothrowSwap>& resource) const noexcept {
        if (cleaned) {
            cleaned->emplace_back(owner, resource.value);
        }
    }

    friend void swap(swap_tracked_deleter& lhs, swap_tracked_deleter& rhs) noexcept(NothrowSwap) {
        if constexpr (!NothrowSwap) {
            throw std::runtime_error("deleter swap must not be called");
        } else {
            if (auto* count = lhs.swaps ? lhs.swaps : rhs.swaps) {
                ++*count;
            }
            std::swap(lhs.owner, rhs.owner);
            std::swap(lhs.cleaned, rhs.cleaned);
            std::swap(lhs.swaps, rhs.swaps);
        }
    }
};

} // namespace

TEST(UniqueResourceRuntimeTest, BasicUsage) {
    int cleanup_count = 0;
    {
        std::unique_resource resource(42, counting_deleter{&cleanup_count});
        EXPECT_EQ(resource.get(), 42);
    }
    EXPECT_EQ(cleanup_count, 1);
}

TEST(UniqueResourceRuntimeTest, ResetRunsDeleterOnce) {
    int cleanup_count = 0;

    std::unique_resource resource(42, counting_deleter{&cleanup_count});
    resource.reset();
    resource.reset();

    EXPECT_EQ(cleanup_count, 1);
}

TEST(UniqueResourceRuntimeTest, DefaultConstructionDoesNotOwnResource) {
    static_counting_deleter::calls = 0;

    {
        std::unique_resource<int, static_counting_deleter> resource;
        EXPECT_EQ(resource.get(), 0);
    }

    EXPECT_EQ(static_counting_deleter::calls, 0);
}

TEST(UniqueResourceRuntimeTest, DefaultConstructedResourceCanBeActivatedWithReset) {
    static_counting_deleter::calls = 0;

    {
        std::unique_resource<int, static_counting_deleter> resource;
        resource.reset(42);
        EXPECT_EQ(resource.get(), 42);
    }

    EXPECT_EQ(static_counting_deleter::calls, 1);
}

TEST(UniqueResourceRuntimeTest, MoveTransfersOwnership) {
    int cleanup_count = 0;
    {
        std::unique_resource original(42, counting_deleter{&cleanup_count});
        auto moved = std::move(original);
        EXPECT_EQ(moved.get(), 42);
    }
    EXPECT_EQ(cleanup_count, 1);
}

TEST(UniqueResourceRuntimeTest, SelfMoveAssignmentReleasesOwnership) {
    int cleanup_count = 0;

    {
        std::unique_resource resource(42, counting_deleter{&cleanup_count});
        resource = std::move(resource);

        EXPECT_EQ(resource.get(), 42);
        EXPECT_EQ(resource.get_deleter().calls, &cleanup_count);
        EXPECT_EQ(cleanup_count, 1);
    }

    EXPECT_EQ(cleanup_count, 1);
}

TEST(UniqueResourceRuntimeTest, SwapExchangesResourceAndDeleter) {
    int cleanup_count1 = 0;
    int cleanup_count2 = 0;

    {
        std::unique_resource resource1(1, counting_deleter{&cleanup_count1});
        std::unique_resource resource2(2, counting_deleter{&cleanup_count2});

        resource1.swap(resource2);

        EXPECT_EQ(resource1.get(), 2);
        EXPECT_EQ(resource2.get(), 1);
        EXPECT_EQ(resource1.get_deleter().calls, &cleanup_count2);
        EXPECT_EQ(resource2.get_deleter().calls, &cleanup_count1);
    }

    EXPECT_EQ(cleanup_count1, 1);
    EXPECT_EQ(cleanup_count2, 1);
}

TEST(UniqueResourceRuntimeTest, SelfSwapIsNoOp) {
    int cleanup_count = 0;

    {
        std::unique_resource resource(42, counting_deleter{&cleanup_count});
        resource.swap(resource);

        EXPECT_EQ(resource.get(), 42);
        EXPECT_EQ(resource.get_deleter().calls, &cleanup_count);
    }

    EXPECT_EQ(cleanup_count, 1);
}

// T-1: release() disables cleanup on destruction
TEST(UniqueResourceRuntimeTest, ReleaseDisablesCleanup) {
    int cleanup_count = 0;
    {
        std::unique_resource resource(42, counting_deleter{&cleanup_count});
        resource.release();
    }
    EXPECT_EQ(cleanup_count, 0);
}

// T-2: move assignment transfers ownership and cleans up the target
TEST(UniqueResourceRuntimeTest, MoveAssignmentTransfersOwnership) {
    int cleanup_count1 = 0;
    int cleanup_count2 = 0;
    {
        std::unique_resource resource1(1, counting_deleter{&cleanup_count1});
        std::unique_resource resource2(2, counting_deleter{&cleanup_count2});
        resource2 = std::move(resource1);
        EXPECT_EQ(resource2.get(), 1);
    }
    EXPECT_EQ(cleanup_count1, 1);
    EXPECT_EQ(cleanup_count2, 1);
}

// T-3: reset(R) cleans old value and holds new value
TEST(UniqueResourceRuntimeTest, ResetWithNewValueCleansOldAndHoldsNew) {
    std::vector<int> cleaned_values;
    {
        std::unique_resource resource(10, recording_deleter{&cleaned_values});
        resource.reset(20);
        // Old value 10 should have been cleaned
        ASSERT_EQ(cleaned_values.size(), 1u);
        EXPECT_EQ(cleaned_values[0], 10);
        EXPECT_EQ(resource.get(), 20);
    }
    // Destructor cleans new value 20
    ASSERT_EQ(cleaned_values.size(), 2u);
    EXPECT_EQ(cleaned_values[1], 20);
}

// T-4: operator* and operator-> for pointer-like resources
TEST(UniqueResourceRuntimeTest, DereferenceAndArrowOperators) {
    int cleanup_count = 0;
    {
        std::unique_resource resource(
            new simple_object{3, 7}, pointer_deleter{&cleanup_count});
        EXPECT_EQ((*resource).x, 3);
        EXPECT_EQ(resource->y, 7);
    }
    EXPECT_EQ(cleanup_count, 1);
}

// T-8: ADL swap works via using-declaration
TEST(UniqueResourceRuntimeTest, AdlSwapWorks) {
    int cleanup_count1 = 0;
    int cleanup_count2 = 0;

    {
        std::unique_resource r1(1, counting_deleter{&cleanup_count1});
        std::unique_resource r2(2, counting_deleter{&cleanup_count2});

        using std::swap;
        swap(r1, r2);

        EXPECT_EQ(r1.get(), 2);
        EXPECT_EQ(r2.get(), 1);
        EXPECT_EQ(r1.get_deleter().calls, &cleanup_count2);
        EXPECT_EQ(r2.get_deleter().calls, &cleanup_count1);
    }

    EXPECT_EQ(cleanup_count1, 1);
    EXPECT_EQ(cleanup_count2, 1);
}

TEST(UniqueResourceRuntimeTest, NothrowAdlSwapPreservesOwnershipStates) {
    using handle_t = swap_tracked_handle<true>;
    using deleter_t = swap_tracked_deleter<true>;
    for (bool left_owns : {false, true}) {
        for (bool right_owns : {false, true}) {
            SCOPED_TRACE(left_owns);
            SCOPED_TRACE(right_owns);
            int resource_swaps = 0;
            int deleter_swaps = 0;
            std::vector<std::pair<int, int>> cleaned;
            {
                std::unique_resource left(
                    handle_t{1, &resource_swaps}, deleter_t{1, &cleaned, &deleter_swaps});
                std::unique_resource right(
                    handle_t{2, &resource_swaps}, deleter_t{2, &cleaned, &deleter_swaps});
                if (!left_owns) left.release();
                if (!right_owns) right.release();

                if (left_owns) {
                    left.swap(right);
                } else {
                    using std::swap;
                    swap(left, right);
                }

                EXPECT_EQ(resource_swaps, 1);
                EXPECT_EQ(deleter_swaps, 1);
                EXPECT_EQ(left.get().value, 2);
                EXPECT_EQ(right.get().value, 1);
                EXPECT_EQ(left.get_deleter().owner, 2);
                EXPECT_EQ(right.get_deleter().owner, 1);
                EXPECT_TRUE(cleaned.empty());
            }
            std::vector<std::pair<int, int>> expected;
            if (left_owns) expected.emplace_back(1, 1);
            if (right_owns) expected.emplace_back(2, 2);
            EXPECT_EQ(cleaned, expected);
        }
    }
}

TEST(UniqueResourceRuntimeTest, NothrowAdlSwapWithDefaultEmptyResource) {
    using handle_t = swap_tracked_handle<true>;
    using deleter_t = swap_tracked_deleter<true>;
    int resource_swaps = 0;
    int deleter_swaps = 0;
    std::vector<std::pair<int, int>> cleaned;
    {
        std::unique_resource active(
            handle_t{42, &resource_swaps}, deleter_t{42, &cleaned, &deleter_swaps});
        std::unique_resource<handle_t, deleter_t> empty;
        using std::swap;
        swap(active, empty);

        EXPECT_EQ(resource_swaps, 1);
        EXPECT_EQ(deleter_swaps, 1);
        EXPECT_EQ(active.get().value, 0);
        EXPECT_EQ(empty.get().value, 42);
        active.reset();
        EXPECT_TRUE(cleaned.empty());
        empty.reset();
    }
    const std::vector<std::pair<int, int>> expected{{42, 42}};
    EXPECT_EQ(cleaned, expected);
}

TEST(UniqueResourceRuntimeTest, ReferenceSwapRebindsWithoutSwappingThrowingReferents) {
    using handle_t = swap_tracked_handle<false>;
    using deleter_t = swap_tracked_deleter<true>;
    static_assert(!std::is_nothrow_swappable_v<handle_t>);
    int resource_swaps = 0;
    int deleter_swaps = 0;
    handle_t first{1, &resource_swaps};
    handle_t second{2, &resource_swaps};
    std::vector<std::pair<int, int>> cleaned;
    {
        std::unique_resource<handle_t&, deleter_t> left(
            first, deleter_t{1, &cleaned, &deleter_swaps});
        std::unique_resource<handle_t&, deleter_t> right(
            second, deleter_t{2, &cleaned, &deleter_swaps});
        using std::swap;
        swap(left, right);

        EXPECT_EQ(&left.get(), &second);
        EXPECT_EQ(&right.get(), &first);
        EXPECT_EQ(first.value, 1);
        EXPECT_EQ(second.value, 2);
        EXPECT_EQ(resource_swaps, 0);
        EXPECT_EQ(deleter_swaps, 1);
    }
    const std::vector<std::pair<int, int>> expected{{1, 1}, {2, 2}};
    EXPECT_EQ(cleaned, expected);
}

TEST(UniqueResourceRuntimeTest, ThrowingComponentSwapsDoNotDisableOtherOperations) {
    const auto check = []<bool ResourceNothrowSwap, bool DeleterNothrowSwap>() {
        using handle_t = swap_tracked_handle<ResourceNothrowSwap>;
        using deleter_t = swap_tracked_deleter<DeleterNothrowSwap>;
        int resource_swaps = 0;
        int deleter_swaps = 0;
        std::vector<std::pair<int, int>> cleaned;
        {
            std::unique_resource original(
                handle_t{1, &resource_swaps}, deleter_t{1, &cleaned, &deleter_swaps});
            auto moved = std::move(original);
            moved.reset(handle_t{3, &resource_swaps});
            moved.release();

            std::unique_resource target(
                handle_t{2, &resource_swaps}, deleter_t{2, &cleaned, &deleter_swaps});
            target = std::move(moved);
            using std::swap;
            swap(original, target);
            target.reset(handle_t{4, &resource_swaps});
        }
        const std::vector<std::pair<int, int>> expected{{1, 1}, {2, 2}, {1, 4}};
        EXPECT_EQ(cleaned, expected);
        EXPECT_EQ(resource_swaps, 0);
        EXPECT_EQ(deleter_swaps, 0);
    };
    check.template operator()<false, true>();
    check.template operator()<true, false>();
    check.template operator()<false, false>();
}
