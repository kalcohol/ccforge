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

struct immovable_scheduler_env {
    std::execution::inline_scheduler scheduler;
    int* alive;

    immovable_scheduler_env(std::execution::inline_scheduler sch, int* count) noexcept
        : scheduler(sch), alive(count) {
        ++*alive;
    }

    immovable_scheduler_env(const immovable_scheduler_env&) = delete;
    immovable_scheduler_env(immovable_scheduler_env&&) = delete;

    ~immovable_scheduler_env() {
        --*alive;
    }

    auto query(std::execution::get_scheduler_t) const noexcept {
        EXPECT_GT(*alive, 0);
        return scheduler;
    }
};

template<class Env>
struct borrowed_scheduler_receiver : scheduler_receiver {
    Env* env;

    auto get_env() const noexcept -> Env& {
        return *env;
    }
};

struct owned_scheduler_receiver : scheduler_receiver {
    int* alive;

    auto get_env() const noexcept -> immovable_scheduler_env {
        return immovable_scheduler_env{expected, alive};
    }
};

static_assert(!std::is_copy_constructible_v<immovable_scheduler_env>);
static_assert(!std::is_move_constructible_v<immovable_scheduler_env>);
static_assert(std::execution::receiver<owned_scheduler_receiver>);
static_assert(std::is_same_v<std::execution::env_of_t<owned_scheduler_receiver>,
                           immovable_scheduler_env>);

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

struct member_stop_env {
    std::inplace_stop_token token;

    auto query(std::execution::get_stop_token_t) const noexcept
        -> std::inplace_stop_token {
        return token;
    }
};

struct member_stop_error_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::inplace_stop_source* source;
    bool* errored;

    template<class... Vs>
    void set_value(Vs&&...) && noexcept {}

    template<class E>
    void set_error(E&&) && noexcept {
        *errored = true;
    }

    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> member_stop_env {
        return member_stop_env{source->get_token()};
    }
};

struct value_error_receiver {
    using receiver_concept = std::execution::receiver_t;

    int* value;
    bool* errored;

    friend void tag_invoke(
        std::execution::set_value_t,
        value_error_receiver&& self,
        int result) noexcept {
        *self.value = result;
    }

    friend void tag_invoke(
        std::execution::set_error_t,
        value_error_receiver&& self,
        std::exception_ptr) noexcept {
        *self.errored = true;
    }

    friend auto tag_invoke(
        std::execution::get_env_t,
        const value_error_receiver&) noexcept -> std::execution::empty_env {
        return {};
    }
};

template<bool Forwarding>
struct child_query {
    constexpr bool query(std::forwarding_query_t) const noexcept {
        return Forwarding;
    }

    template<class Env>
        requires (requires(const Env& env) { env.query(std::declval<child_query>()); } ||
                  requires(const Env& env) { tag_invoke(std::declval<child_query>(), env); })
    int operator()(const Env& env) const noexcept {
        if constexpr (requires { env.query(*this); }) {
            return env.query(*this);
        } else {
            return tag_invoke(*this, env);
        }
    }
};

struct member_child_query_env {
    template<bool Forwarding>
    int query(child_query<Forwarding>) const noexcept {
        return 7;
    }

    template<bool Forwarding>
    friend int tag_invoke(child_query<Forwarding>,
                          const member_child_query_env&) noexcept {
        return -7;
    }
};

struct tag_child_query_env {
    template<bool Forwarding>
    friend int tag_invoke(child_query<Forwarding>,
                          const tag_child_query_env&) noexcept {
        return 7;
    }
};

template<bool Forwarding>
struct observe_child_query {
    template<class Env>
    auto operator()(const Env& env) const noexcept {
        if constexpr (requires { child_query<Forwarding>{}(env); }) {
            return child_query<Forwarding>{}(env);
        } else {
            return false;
        }
    }
};

template<class Env>
struct query_env_receiver : value_error_receiver {
    Env env;

    auto get_env() const noexcept -> Env {
        return env;
    }
};

static_assert(std::is_empty_v<child_query<true>>);
static_assert(std::is_empty_v<child_query<false>>);
static_assert(std::forwarding_query(child_query<true>{}));
static_assert(!std::forwarding_query(child_query<false>{}));

template<bool Forwarding, class Env>
void check_child_query_filter() {
    int value = -1;
    bool errored = false;
    using receiver_t = query_env_receiver<Env>;
    static_assert(std::execution::receiver<receiver_t>);

    auto direct_sender = std::execution::read_env(observe_child_query<Forwarding>{});
    using direct_cs_t = decltype(std::execution::get_completion_signatures(
        direct_sender, std::declval<Env>()));
    static_assert(std::is_same_v<direct_cs_t, std::execution::completion_signatures<
        std::execution::set_value_t(int)>>);
    auto direct = std::execution::connect(
        std::move(direct_sender), receiver_t{{&value, &errored}, {}});
    std::execution::start(direct);
    EXPECT_EQ(value, 7);
    EXPECT_FALSE(errored);

    value = -1;
    auto sndr = std::execution::when_all(
        std::execution::read_env(observe_child_query<Forwarding>{}));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::declval<Env>()));
    using result_t = std::conditional_t<Forwarding, int, bool>;
    static_assert(std::is_same_v<cs_t, std::execution::completion_signatures<
        std::execution::set_value_t(result_t)>>);
    auto op = std::execution::connect(
        std::move(sndr), receiver_t{{&value, &errored}, {}});
    std::execution::start(op);
    EXPECT_EQ(value, Forwarding ? 7 : 0);
    EXPECT_FALSE(errored);
}

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

TEST(WhenAllTest, DoesNotRequireUndeclaredStoppedCompletion) {
    int value = 0;
    bool errored = false;
    auto op = std::execution::connect(
        std::execution::when_all(std::execution::just(42)),
        value_error_receiver{&value, &errored});

    std::execution::start(op);

    EXPECT_EQ(value, 42);
    EXPECT_FALSE(errored);
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

TEST(WhenAllTest, ChildEnvBorrowsStableNoncopyableOuterEnv) {
    std::execution::inline_scheduler sch;
    int alive = 0;

    auto check = [&]<class Env>(Env& env) {
        using receiver_t = borrowed_scheduler_receiver<Env>;
        static_assert(std::execution::receiver<receiver_t>);
        static_assert(std::is_same_v<std::execution::env_of_t<receiver_t>, Env&>);
        bool got_scheduler = false;
        bool completed = false;

        auto direct = std::execution::connect(
            std::execution::read_env(std::execution::get_scheduler),
            receiver_t{{sch, &got_scheduler, &completed}, &env});
        std::execution::start(direct);
        EXPECT_TRUE(completed);
        EXPECT_TRUE(got_scheduler);

        got_scheduler = false;
        completed = false;
        auto sndr = std::execution::when_all(
            std::execution::read_env(std::execution::get_scheduler));
        using cs_t = decltype(std::execution::get_completion_signatures(
            sndr, std::declval<Env&>()));
        static_assert(std::is_same_v<cs_t, std::execution::completion_signatures<
            std::execution::set_value_t(std::execution::inline_scheduler)>>);
        auto op = std::execution::connect(
            std::move(sndr), receiver_t{{sch, &got_scheduler, &completed}, &env});
        std::execution::start(op);
        EXPECT_TRUE(completed);
        EXPECT_TRUE(got_scheduler);
        EXPECT_EQ(alive, 1);
    };

    {
        immovable_scheduler_env env{sch, &alive};
        check(env);
    }
    EXPECT_EQ(alive, 0);
    {
        const immovable_scheduler_env env{sch, &alive};
        check(env);
    }
    EXPECT_EQ(alive, 0);
}

TEST(WhenAllTest, ChildEnvOwnsImmovablePrvalueOuterEnv) {
    std::execution::inline_scheduler sch;
    int alive = 0;
    bool got_scheduler = false;
    bool completed = false;

    auto direct = std::execution::connect(
        std::execution::read_env(std::execution::get_scheduler),
        owned_scheduler_receiver{{sch, &got_scheduler, &completed}, &alive});
    std::execution::start(direct);
    EXPECT_TRUE(completed);
    EXPECT_TRUE(got_scheduler);
    EXPECT_EQ(alive, 0);

    got_scheduler = false;
    completed = false;
    auto sndr = std::execution::when_all(
        std::execution::read_env(std::execution::get_scheduler));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::declval<immovable_scheduler_env>()));
    static_assert(std::is_same_v<cs_t, std::execution::completion_signatures<
        std::execution::set_value_t(std::execution::inline_scheduler)>>);
    auto op = std::execution::connect(
        std::move(sndr),
        owned_scheduler_receiver{{sch, &got_scheduler, &completed}, &alive});
    std::execution::start(op);
    EXPECT_TRUE(completed);
    EXPECT_TRUE(got_scheduler);
    EXPECT_EQ(alive, 0);
}

TEST(WhenAllTest, ChildEnvFiltersNonforwardingMemberQueries) {
    check_child_query_filter<true, member_child_query_env>();
    check_child_query_filter<false, member_child_query_env>();
}

TEST(WhenAllTest, ChildEnvFiltersNonforwardingTagQueries) {
    check_child_query_filter<true, tag_child_query_env>();
    check_child_query_filter<false, tag_child_query_env>();
}

TEST(WhenAllTest, ChildEnvOverridesOuterStopToken) {
    std::inplace_stop_source source;
    int value = -1;
    bool errored = false;
    auto observes_child_token = [outer = source.get_token()](const auto& env) noexcept {
        auto token = std::execution::get_stop_token(env);
        static_assert(std::is_same_v<decltype(token), std::inplace_stop_token>);
        return int(token != outer);
    };
    using receiver_t = query_env_receiver<member_stop_env>;

    auto direct = std::execution::connect(
        std::execution::read_env(observes_child_token),
        receiver_t{{&value, &errored}, {source.get_token()}});
    std::execution::start(direct);
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(errored);

    value = -1;
    auto op = std::execution::connect(
        std::execution::when_all(std::execution::read_env(observes_child_token)),
        receiver_t{{&value, &errored}, {source.get_token()}});
    std::execution::start(op);
    EXPECT_EQ(value, 1);
    EXPECT_FALSE(errored);
    EXPECT_FALSE(source.stop_requested());
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

TEST(WhenAllTest, ErrorOverridesPriorStopped) {
    auto sndr = std::execution::when_all(
        std::execution::just_stopped(),
        std::execution::just_error(42));
    EXPECT_THROW((void)std::execution::sync_wait(std::move(sndr)), int);
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

TEST(WhenAllTest, MemberQueryEnvCannotHideSiblingCancellation) {
    std::inplace_stop_source outer_source;
    int observed_stops = 0;
    bool errored = false;

    auto op = std::execution::connect(
        std::execution::when_all(
            std::execution::just_error(42),
            stop_observing_sender{&observed_stops}),
        member_stop_error_receiver{&outer_source, &errored});

    std::execution::start(op);

    EXPECT_EQ(observed_stops, 1);
    EXPECT_TRUE(errored);
    if (observed_stops == 0) {
        outer_source.request_stop();
    }
}
