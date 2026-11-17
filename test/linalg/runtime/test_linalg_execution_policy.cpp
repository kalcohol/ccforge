#include "../extended_cases.hpp"
#include "../proxy_cases.hpp"
#include <atomic>
#include <cstdlib>
#include <exception>
#include <thread>

#if !FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS
#error "Register this target only when native execution policies exist"
#endif

namespace {
struct access_failure {};
struct throwing_accessor : std::default_accessor<double> {
    using offset_policy = throwing_accessor;
    reference access(data_handle_type, std::size_t) const { throw access_failure{}; }
};
struct throwing_value {
    throwing_value& operator+=(throwing_value) { return *this; }
    friend throwing_value operator*(throwing_value, throwing_value) { throw access_failure{}; }
};
struct thread_accessor : std::default_accessor<double> {
    using offset_policy = thread_accessor;
    std::thread::id expected;
    std::atomic<bool>* wrong_thread;
    reference access(data_handle_type p, std::size_t i) const {
        if (std::this_thread::get_id() != expected) wrong_thread->store(true, std::memory_order_relaxed);
        return p[i];
    }
};

template<class Policy> void terminate_case(const Policy& policy, int which) {
    std::set_terminate([] { std::_Exit(86); });
    double data[]{2}, out[]{3};
    using V = std::extents<int, 1>;
    using M = std::extents<int, 1, 1>;
    std::mdspan<double, V, std::layout_right, throwing_accessor> v(data);
    std::mdspan<double, M, std::layout_right, throwing_accessor> a(data);
    std::mdspan x(out, V{});
    std::mdspan c(out, M{});
    using namespace std::linalg;
    try {
        switch (which) {
        case 0: copy(policy, v, x); break;
        case 1: scale(policy, 2.0, v); break;
        case 2: (void)dot(policy, v, x, 0.0); break;
        case 3: (void)vector_two_norm(policy, v); break;
        case 4: matrix_vector_product(policy, a, v, x); break;
        case 5: triangular_matrix_vector_solve(policy, a, upper_triangle, explicit_diagonal, x); break;
        case 6: matrix_product(policy, a, a, c); break;
        case 7: triangular_matrix_left_product(policy, a, upper_triangle, explicit_diagonal, c); break;
        case 8: symmetric_matrix_rank_k_update(policy, 2.0, a, c, upper_triangle); break;
        case 9: {
            throwing_value storage[2];
            std::mdspan first(storage, V{});
            std::mdspan second(storage + 1, V{});
            (void)dot(policy, first, second, throwing_value{});
            break;
        }
        }
    } catch (...) { std::_Exit(87); }
    std::_Exit(88);
}

template<class Policy> void policy_cases(const Policy& policy) {
    linalg_conformance::policy_calls<Policy> calls{policy};
    linalg_conformance::scalar_cases(calls);
    linalg_conformance::extended_cases(calls);
    linalg_conformance::mixed_rhs_proxy_solves(calls);
    linalg_conformance::assignment_only_outputs(calls);
    linalg_conformance::complex_proxy_dotc(calls);
    // Calling-thread execution is a Forge guarantee for every native policy.
    std::atomic<bool> wrong{false};
    double input[]{2}, output[]{0};
    using E = std::extents<int, 1>;
    thread_accessor accessor{{}, std::this_thread::get_id(), &wrong};
    std::mdspan<double, E, std::layout_right, thread_accessor> x(input, E{}, accessor);
    std::mdspan<double, E, std::layout_right, thread_accessor> y(output, E{}, accessor);
    std::linalg::copy(policy, x, y);
    EXPECT_EQ(output[0], 2.0);
    EXPECT_FALSE(wrong.load(std::memory_order_relaxed));
}

template<class Policy> void death_cases(const Policy& policy) {
#if GTEST_HAS_DEATH_TEST
    for (int which = 0; which != 10; ++which) {
        SCOPED_TRACE(which);
        ASSERT_EXIT(terminate_case(policy, which), ::testing::ExitedWithCode(86), "");
    }
#else
    (void)policy;
    GTEST_SKIP() << "GoogleTest death tests unavailable: terminate checks require a subprocess";
#endif
}
} // namespace

TEST(LinalgExecutionPolicy, Seq) { policy_cases(std::execution::seq); }
TEST(LinalgExecutionPolicy, Par) { policy_cases(std::execution::par); }
TEST(LinalgExecutionPolicy, ParUnseq) { policy_cases(std::execution::par_unseq); }
TEST(LinalgExecutionPolicyDeathTest, Seq) { death_cases(std::execution::seq); }
TEST(LinalgExecutionPolicyDeathTest, Par) { death_cases(std::execution::par); }
TEST(LinalgExecutionPolicyDeathTest, ParUnseq) { death_cases(std::execution::par_unseq); }
#if __cpp_lib_execution >= 201902L
TEST(LinalgExecutionPolicy, Unseq) { policy_cases(std::execution::unseq); }
TEST(LinalgExecutionPolicyDeathTest, Unseq) { death_cases(std::execution::unseq); }
#endif

TEST(LinalgExecutionPolicy, NonPolicyAccessExceptionPropagates) {
    double data[]{1}, output[]{0};
    using E = std::extents<int, 1>;
    std::mdspan<double, E, std::layout_right, throwing_accessor> x(data);
    std::mdspan y(output, E{});
    EXPECT_THROW(std::linalg::copy(x, y), access_failure);
}
