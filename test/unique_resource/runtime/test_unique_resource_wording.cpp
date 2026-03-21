#include <gtest/gtest.h>

#include <stdexcept>

#include <memory>

namespace {

struct resource_copy_fallback {
    static int copy_construct_count;
    static int move_construct_count;
    static int copy_assign_count;
    static int move_assign_count;

    int value;

    explicit resource_copy_fallback(int initial_value) noexcept
        : value(initial_value) {}

    resource_copy_fallback(const resource_copy_fallback& other) noexcept
        : value(other.value) {
        ++copy_construct_count;
    }

    resource_copy_fallback(resource_copy_fallback&& other)
        : value(other.value) {
        ++move_construct_count;
        throw std::runtime_error("resource move construction should not be used");
    }

    resource_copy_fallback& operator=(const resource_copy_fallback& other) noexcept {
        value = other.value;
        ++copy_assign_count;
        return *this;
    }

    resource_copy_fallback& operator=(resource_copy_fallback&& other) {
        value = other.value;
        ++move_assign_count;
        throw std::runtime_error("resource move assignment should not be used");
    }
};

int resource_copy_fallback::copy_construct_count = 0;
int resource_copy_fallback::move_construct_count = 0;
int resource_copy_fallback::copy_assign_count = 0;
int resource_copy_fallback::move_assign_count = 0;

void reset_resource_copy_fallback_counts() {
    resource_copy_fallback::copy_construct_count = 0;
    resource_copy_fallback::move_construct_count = 0;
    resource_copy_fallback::copy_assign_count = 0;
    resource_copy_fallback::move_assign_count = 0;
}

struct resource_noop_deleter {
    void operator()(const resource_copy_fallback&) const noexcept {}
};

struct deleter_copy_fallback {
    static int copy_construct_count;
    static int move_construct_count;
    static int copy_assign_count;
    static int move_assign_count;

    int* calls;

    explicit deleter_copy_fallback(int* cleanup_calls = nullptr) noexcept
        : calls(cleanup_calls) {}

    deleter_copy_fallback(const deleter_copy_fallback& other) noexcept
        : calls(other.calls) {
        ++copy_construct_count;
    }

    deleter_copy_fallback(deleter_copy_fallback&& other)
        : calls(other.calls) {
        ++move_construct_count;
        throw std::runtime_error("deleter move construction should not be used");
    }

    deleter_copy_fallback& operator=(const deleter_copy_fallback& other) noexcept {
        calls = other.calls;
        ++copy_assign_count;
        return *this;
    }

    deleter_copy_fallback& operator=(deleter_copy_fallback&& other) {
        calls = other.calls;
        ++move_assign_count;
        throw std::runtime_error("deleter move assignment should not be used");
    }

    void operator()(int) const noexcept {
        if (calls != nullptr) {
            ++*calls;
        }
    }
};

int deleter_copy_fallback::copy_construct_count = 0;
int deleter_copy_fallback::move_construct_count = 0;
int deleter_copy_fallback::copy_assign_count = 0;
int deleter_copy_fallback::move_assign_count = 0;

void reset_deleter_copy_fallback_counts() {
    deleter_copy_fallback::copy_construct_count = 0;
    deleter_copy_fallback::move_construct_count = 0;
    deleter_copy_fallback::copy_assign_count = 0;
    deleter_copy_fallback::move_assign_count = 0;
}

struct reference_cleanup {
    int* cleaned_value;

    void operator()(int& value) const noexcept {
        *cleaned_value = value;
    }
};

} // namespace

TEST(UniqueResourceWordingTest, MoveConstructorCopiesResourceWhenMoveMayThrow) {
    reset_resource_copy_fallback_counts();

    std::unique_resource original(resource_copy_fallback{42}, resource_noop_deleter{});

    EXPECT_NO_THROW({
        auto moved = std::move(original);
        EXPECT_EQ(moved.get().value, 42);
    });

    // T-9: relax exact count — at least one copy is required by the standard
    EXPECT_GE(resource_copy_fallback::copy_construct_count, 1);
    EXPECT_EQ(resource_copy_fallback::move_construct_count, 0);
}

TEST(UniqueResourceWordingTest, ConstructorCopiesResourceWhenMoveMayThrow) {
    resource_copy_fallback source(42);

    reset_resource_copy_fallback_counts();

    EXPECT_NO_THROW({
        auto resource = std::unique_resource(std::move(source), resource_noop_deleter{});
        EXPECT_EQ(resource.get().value, 42);
    });

    EXPECT_EQ(resource_copy_fallback::copy_construct_count, 1);
    EXPECT_EQ(resource_copy_fallback::move_construct_count, 0);
}

TEST(UniqueResourceWordingTest, MoveAssignmentCopiesResourceWhenMoveMayThrow) {
    std::unique_resource source(resource_copy_fallback{42}, resource_noop_deleter{});
    std::unique_resource target(resource_copy_fallback{7}, resource_noop_deleter{});

    reset_resource_copy_fallback_counts();

    EXPECT_NO_THROW({
        target = std::move(source);
    });

    EXPECT_EQ(target.get().value, 42);
    EXPECT_EQ(resource_copy_fallback::copy_assign_count, 1);
    EXPECT_EQ(resource_copy_fallback::move_assign_count, 0);
}

TEST(UniqueResourceWordingTest, MoveConstructorCopiesDeleterWhenMoveMayThrow) {
    int cleanup_count = 0;
    std::unique_resource original(42, deleter_copy_fallback{&cleanup_count});

    reset_deleter_copy_fallback_counts();

    EXPECT_NO_THROW({
        auto moved = std::move(original);
        EXPECT_EQ(moved.get(), 42);
        EXPECT_EQ(moved.get_deleter().calls, &cleanup_count);
    });

    EXPECT_EQ(deleter_copy_fallback::copy_construct_count, 1);
    EXPECT_EQ(deleter_copy_fallback::move_construct_count, 0);
}

TEST(UniqueResourceWordingTest, ConstructorCopiesDeleterWhenMoveMayThrow) {
    deleter_copy_fallback deleter;

    reset_deleter_copy_fallback_counts();

    EXPECT_NO_THROW({
        auto resource = std::unique_resource(42, std::move(deleter));
        EXPECT_EQ(resource.get(), 42);
    });

    EXPECT_EQ(deleter_copy_fallback::copy_construct_count, 1);
    EXPECT_EQ(deleter_copy_fallback::move_construct_count, 0);
}

TEST(UniqueResourceWordingTest, MoveAssignmentCopiesDeleterWhenMoveMayThrow) {
    int source_cleanup_count = 0;
    int target_cleanup_count = 0;

    auto source = std::unique_resource(42, deleter_copy_fallback{&source_cleanup_count});
    auto target = std::unique_resource(7, deleter_copy_fallback{&target_cleanup_count});

    reset_deleter_copy_fallback_counts();

    EXPECT_NO_THROW({
        target = std::move(source);
    });

    EXPECT_EQ(target.get(), 42);
    EXPECT_EQ(target.get_deleter().calls, &source_cleanup_count);
    EXPECT_EQ(deleter_copy_fallback::copy_assign_count, 1);
    EXPECT_EQ(deleter_copy_fallback::move_assign_count, 0);
}

TEST(UniqueResourceWordingTest, ResetRebindsReferenceResource) {
    int cleaned_value = 0;
    int first = 1;
    int second = 2;

    {
        std::unique_resource<int&, reference_cleanup> resource(first, reference_cleanup{&cleaned_value});
        resource.reset(second);

        EXPECT_EQ(&resource.get(), &second);
        EXPECT_EQ(first, 1);
        EXPECT_EQ(second, 2);
    }

    EXPECT_EQ(cleaned_value, 2);
    EXPECT_EQ(first, 1);
}
