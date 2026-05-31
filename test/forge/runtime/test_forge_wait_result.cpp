#include <gtest/gtest.h>
#include <forge/execution.hpp>
#include <execution>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <variant>

namespace {

struct error_code_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_error_t(std::error_code)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;

        void start() & noexcept {
            std::execution::set_error(
                std::move(rcvr),
                std::make_error_code(std::errc::invalid_argument));
        }
    };

    template<std::execution::receiver R>
    auto connect(R rcvr) && -> op<R> {
        return op<R>{std::move(rcvr)};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) const& -> op<R> {
        return op<R>{std::move(rcvr)};
    }
};

struct multi_value_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_value_t(double)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;

        void start() & noexcept {
            std::execution::set_value(std::move(rcvr), 7);
        }
    };

    template<std::execution::receiver R>
    auto connect(R rcvr) && -> op<R> {
        return op<R>{std::move(rcvr)};
    }
};

} // namespace

TEST(ForgeWaitResultTest, CapturesValueTuple) {
    auto result = forge::wait_result(std::execution::just(42));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_FALSE(result.has_error());
    EXPECT_FALSE(result.stopped());
    EXPECT_EQ(std::get<0>(result.value()), 42);
}

TEST(ForgeWaitResultTest, CapturesStopped) {
    auto result = forge::wait_result(std::execution::just_stopped());

    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.has_error());
    EXPECT_TRUE(result.stopped());
}

TEST(ForgeWaitResultTest, CapturesExceptionPtrError) {
    auto error = std::make_exception_ptr(std::runtime_error{"boom"});
    auto result = forge::wait_result(std::execution::just_error(error));

    ASSERT_TRUE(result.has_error());
    auto* captured = result.error_if<std::exception_ptr>();
    ASSERT_NE(captured, nullptr);
    EXPECT_THROW(std::rethrow_exception(*captured), std::runtime_error);
}

TEST(ForgeWaitResultTest, CapturesTypedError) {
    auto result = forge::wait_result(error_code_sender{});

    ASSERT_TRUE(result.has_error());
    auto* error = result.error_if<std::error_code>();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(result.error_if<std::exception_ptr>(), nullptr);
}

TEST(ForgeWaitResultTest, PreservesMultiValueShape) {
    auto result = forge::wait_result(multi_value_sender{});

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<std::tuple<int>>(result.value()));
    EXPECT_EQ(std::get<0>(std::get<std::tuple<int>>(result.value())), 7);
}

TEST(ForgeWaitResultTest, CrossesErasedSenderBoundary) {
    using completions = std::execution::completion_signatures<
        std::execution::set_value_t(),
        std::execution::set_error_t(std::error_code),
        std::execution::set_stopped_t()>;

    forge::erased_sender<completions> sender{error_code_sender{}};
    auto result = forge::wait_result(std::move(sender));

    ASSERT_TRUE(result.has_error());
    auto* error = result.error_if<std::error_code>();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, std::make_error_code(std::errc::invalid_argument));
}
