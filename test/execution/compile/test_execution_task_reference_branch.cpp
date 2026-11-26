// Records which implementation of the optional<U&>::emplace constraint the
// task uses on this toolchain: 1 is std::reference_constructs_from_temporary,
// 2 the compiler builtin and 0 the stricter approximation. The CMake caller
// tries each value and reports the one that compiles.

#include <execution>

#if !defined(FORGE_TASK_REFERENCE_BRANCH)
#error "FORGE_TASK_REFERENCE_BRANCH selects the branch to test"
#endif

static_assert(std::execution::__forge_task::__reference_binding_branch == FORGE_TASK_REFERENCE_BRANCH);

// The cases a co_return operand can produce agree on every branch.
static_assert(std::execution::__forge_task::__binds_temporary<const int&, int>);
static_assert(!std::execution::__forge_task::__binds_temporary<const int&, int&>);
static_assert(!std::execution::__forge_task::__binds_temporary<const int&, const int&>);
static_assert(std::execution::__forge_task::__binds_temporary<const int&, long&>);

struct external {
    int value;
    operator const int&() const noexcept { return value; }
};

// A conversion function that returns a reference binds no temporary. Only
// the two exact branches see that.
static_assert(std::execution::__forge_task::__binds_temporary<const int&, external&> ==
              (FORGE_TASK_REFERENCE_BRANCH == 0));

int main() {}
