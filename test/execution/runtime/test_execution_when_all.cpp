#include <gtest/gtest.h>
#include <execution>
#include <exception>
#include <tuple>
#include <string>
#include <type_traits>

namespace {

struct multi_value_sender {
    using sender_concept = std::execution::sender_t;

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const multi_value_sender&, auto) noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_value_t(double),
            std::execution::set_error_t(short)> {
        return {};
    }

    friend auto tag_invoke(std::execution::get_env_t, const multi_value_sender&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

struct other_multi_value_sender {
    using sender_concept = std::execution::sender_t;

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const other_multi_value_sender&, auto) noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(char),
            std::execution::set_value_t(bool),
            std::execution::set_error_t(long),
            std::execution::set_stopped_t()> {
        return {};
    }

    friend auto tag_invoke(std::execution::get_env_t, const other_multi_value_sender&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

struct scheduler_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::execution::inline_scheduler expected;
    bool* got_scheduler;
    bool* completed;

    friend void tag_invoke(std::execution::set_value_t, scheduler_receiver&& self,
                           std::execution::inline_scheduler sch) noexcept {
        *self.got_scheduler = (sch == self.expected);
        *self.completed = true;
    }

    template<class E>
    friend void tag_invoke(std::execution::set_error_t, scheduler_receiver&& self, E&&) noexcept {
        *self.completed = false;
    }

    friend void tag_invoke(std::execution::set_stopped_t, scheduler_receiver&& self) noexcept {
        *self.completed = false;
    }

    friend auto tag_invoke(std::execution::get_env_t, const scheduler_receiver& self) noexcept {
        return std::execution::make_env(
            std::execution::make_prop(std::execution::get_scheduler_t{}, self.expected));
    }
};

struct int_error_receiver {
    using receiver_concept = std::execution::receiver_t;

    bool* got_int;
    bool* got_exception_ptr;

    template<class... Vs>
    friend void tag_invoke(std::execution::set_value_t, int_error_receiver&&, Vs&&...) noexcept {}

    friend void tag_invoke(std::execution::set_error_t, int_error_receiver&& self, int) noexcept {
        *self.got_int = true;
    }

    friend void tag_invoke(std::execution::set_error_t, int_error_receiver&& self,
                           std::exception_ptr) noexcept {
        *self.got_exception_ptr = true;
    }

    friend void tag_invoke(std::execution::set_stopped_t, int_error_receiver&&) noexcept {}

    friend auto tag_invoke(std::execution::get_env_t, const int_error_receiver&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

template<class R>
struct stop_observing_op {
    using operation_state_concept = std::execution::operation_state_t;
    using env_t = std::execution::env_of_t<R>;
    using token_t = decltype(std::execution::get_stop_token(std::declval<env_t>()));

    struct callback {
        stop_observing_op* self;

        void operator()() const noexcept {
            ++*self->observed_stops;
            std::execution::set_stopped(std::move(self->rcvr));
        }
    };

    using callback_t = std::stop_callback_for_t<token_t, callback>;

    R rcvr;
    int* observed_stops;
    alignas(callback_t) unsigned char callback_buf[sizeof(callback_t)];
    bool callback_alive = false;

    stop_observing_op(R r, int* observed)
        : rcvr(std::move(r)), observed_stops(observed) {}
    stop_observing_op(const stop_observing_op&) = delete;
    stop_observing_op& operator=(const stop_observing_op&) = delete;

    ~stop_observing_op() {
        if (callback_alive) {
            static_cast<callback_t*>(static_cast<void*>(callback_buf))->~callback_t();
        }
    }

    friend void tag_invoke(std::execution::start_t, stop_observing_op& self) noexcept {
        ::new(static_cast<void*>(self.callback_buf)) callback_t(
            std::execution::get_stop_token(std::execution::get_env(self.rcvr)),
            callback{&self});
        self.callback_alive = true;
    }
};

struct stop_observing_sender {
    using sender_concept = std::execution::sender_t;

    int* observed_stops;

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const stop_observing_sender&, auto) noexcept
        -> std::execution::completion_signatures<std::execution::set_stopped_t()> {
        return {};
    }

    template<std::execution::receiver R>
    friend auto tag_invoke(std::execution::connect_t, stop_observing_sender self, R r)
        -> stop_observing_op<R> {
        return stop_observing_op<R>{std::move(r), self.observed_stops};
    }

    friend auto tag_invoke(std::execution::get_env_t, const stop_observing_sender&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

struct outer_stop_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::inplace_stop_source* source;
    bool* stopped;
    bool* errored;

    template<class... Vs>
    friend void tag_invoke(std::execution::set_value_t, outer_stop_receiver&& self, Vs&&...) noexcept {
        *self.errored = true;
    }

    template<class E>
    friend void tag_invoke(std::execution::set_error_t, outer_stop_receiver&& self, E&&) noexcept {
        *self.errored = true;
    }

    friend void tag_invoke(std::execution::set_stopped_t, outer_stop_receiver&& self) noexcept {
        *self.stopped = true;
    }

    friend auto tag_invoke(std::execution::get_env_t, const outer_stop_receiver& self) noexcept {
        return std::execution::make_env(
            std::execution::make_prop(std::execution::get_stop_token_t{}, self.source->get_token()));
    }
};

} // namespace

TEST(WhenAllTest, AggregatesMultipleValues) {
    auto sndr = std::execution::when_all(
        std::execution::just(1),
        std::execution::just(2.0));
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 1);
    EXPECT_DOUBLE_EQ(std::get<1>(*result), 2.0);
}

TEST(WhenAllTest, SingleSender) {
    auto sndr = std::execution::when_all(std::execution::just(42));
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(WhenAllTest, ErrorPropagates) {
    auto sndr = std::execution::when_all(
        std::execution::just(1),
        std::execution::just_error(42));
    EXPECT_THROW(std::execution::sync_wait(std::move(sndr)), int);
}

TEST(WhenAllTest, ReportsCartesianValueAndChildErrorSignatures) {
    auto sndr = std::execution::when_all(
        multi_value_sender{},
        other_multi_value_sender{});
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int, char),
            std::execution::set_value_t(int, bool),
            std::execution::set_value_t(double, char),
            std::execution::set_value_t(double, bool),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_error_t(short),
            std::execution::set_error_t(long),
            std::execution::set_stopped_t()>>);
}

TEST(WhenAllTest, WithVariantReportsSingleVariantValuePerChild) {
    auto sndr = std::execution::when_all_with_variant(
        multi_value_sender{},
        other_multi_value_sender{});
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    using first_variant_t = std::variant<std::tuple<int>, std::tuple<double>>;
    using second_variant_t = std::variant<std::tuple<char>, std::tuple<bool>>;

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(first_variant_t, second_variant_t),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_error_t(short),
            std::execution::set_error_t(long),
            std::execution::set_stopped_t()>>);
}

TEST(WhenAllTest, WithVariantAggregatesRuntimeValues) {
    auto result = std::execution::sync_wait(
        std::execution::when_all_with_variant(
            std::execution::just(1),
            std::execution::just(2.0)));

    ASSERT_TRUE(result.has_value());
    auto& first = std::get<0>(*result);
    auto& second = std::get<1>(*result);
    ASSERT_EQ(first.index(), 0u);
    ASSERT_EQ(second.index(), 0u);
    EXPECT_EQ(std::get<0>(std::get<0>(first)), 1);
    EXPECT_DOUBLE_EQ(std::get<0>(std::get<0>(second)), 2.0);
}

TEST(WhenAllTest, DropsValueSignatureWhenAChildCannotProduceValue) {
    auto sndr = std::execution::when_all(
        std::execution::just_error(42),
        std::execution::just(2));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_error_t(int)>>);
}

TEST(WhenAllTest, ChildEnvPreservesOuterQueries) {
    std::execution::inline_scheduler sch;
    bool got_scheduler = false;
    bool completed = false;

    auto op = std::execution::connect(
        std::execution::when_all(std::execution::read_env(std::execution::get_scheduler)),
        scheduler_receiver{sch, &got_scheduler, &completed});

    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(got_scheduler);
}

TEST(WhenAllTest, ForwardsOriginalErrorType) {
    bool got_int = false;
    bool got_exception_ptr = false;

    auto op = std::execution::connect(
        std::execution::when_all(std::execution::just_error(42)),
        int_error_receiver{&got_int, &got_exception_ptr});

    std::execution::start(op);

    EXPECT_TRUE(got_int);
    EXPECT_FALSE(got_exception_ptr);
}

TEST(WhenAllTest, StoppedPropagates) {
    auto sndr = std::execution::when_all(
        std::execution::just(1),
        std::execution::just_stopped());
    auto result = std::execution::sync_wait(std::move(sndr));
    EXPECT_FALSE(result.has_value());
}

TEST(WhenAllTest, OuterStopRequestReachesChildren) {
    std::inplace_stop_source source;
    int observed_stops = 0;
    bool stopped = false;
    bool errored = false;

    auto op = std::execution::connect(
        std::execution::when_all(
            stop_observing_sender{&observed_stops},
            stop_observing_sender{&observed_stops}),
        outer_stop_receiver{&source, &stopped, &errored});

    std::execution::start(op);
    EXPECT_EQ(observed_stops, 0);
    EXPECT_FALSE(stopped);

    EXPECT_TRUE(source.request_stop());

    EXPECT_EQ(observed_stops, 2);
    EXPECT_TRUE(stopped);
    EXPECT_FALSE(errored);
}
