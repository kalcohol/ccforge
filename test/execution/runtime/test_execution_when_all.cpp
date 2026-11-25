#include <gtest/gtest.h>
#include "observe_completion.hpp"
#include <execution>
#include <exception>
#include <tuple>
#include <string>
#include <type_traits>

namespace {

struct multi_value_sender {
    using sender_concept = std::execution::sender_tag;

    template<class Self, class Env = std::execution::env<>>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_value_t(double),
            std::execution::set_error_t(short)> {
        return {};
    }

    auto get_env() const noexcept
        -> std::execution::env<> {
        return {};
    }
};

struct other_multi_value_sender {
    using sender_concept = std::execution::sender_tag;

    template<class Self, class Env = std::execution::env<>>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(char),
            std::execution::set_value_t(bool),
            std::execution::set_error_t(long),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept
        -> std::execution::env<> {
        return {};
    }
};

struct scheduler_receiver {
    using receiver_concept = std::execution::receiver_tag;

    std::execution::inline_scheduler expected;
    bool* got_scheduler;
    bool* completed;

    void set_value(std::execution::inline_scheduler sch) && noexcept {
        *this->got_scheduler = (sch == this->expected);
        *this->completed = true;
    }

    template<class E>
    void set_error(E&&) && noexcept {
        *this->completed = false;
    }

    void set_stopped() && noexcept {
        *this->completed = false;
    }

    auto get_env() const noexcept {
        return std::execution::env{
            std::execution::prop(std::execution::get_scheduler_t{}, this->expected)};
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
    using receiver_concept = std::execution::receiver_tag;

    bool* got_int;
    bool* got_exception_ptr;

    template<class... Vs>
    void set_value(Vs&&...) && noexcept {}

    void set_error(int) && noexcept {
        *this->got_int = true;
    }

    void set_error(std::exception_ptr) && noexcept {
        *this->got_exception_ptr = true;
    }

    void set_stopped() && noexcept {}

    auto get_env() const noexcept
        -> std::execution::env<> {
        return {};
    }
};

template<class R>
struct stop_observing_op {
    using operation_state_concept = std::execution::operation_state_tag;
    using env_t = std::execution::env_of_t<R>;
    using token_t = decltype(std::get_stop_token(std::declval<env_t>()));

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

    void start() & noexcept {
        ::new(static_cast<void*>(this->callback_buf)) callback_t(
            std::get_stop_token(std::execution::get_env(this->rcvr)),
            callback{this});
        this->callback_alive = true;
    }
};

struct stop_observing_sender {
    using sender_concept = std::execution::sender_tag;

    int* observed_stops;

    template<class Self, class Env = std::execution::env<>>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_stopped_t()> {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R r) const
        -> stop_observing_op<R> {
        return stop_observing_op<R>{std::move(r), observed_stops};
    }

    auto get_env() const noexcept
        -> std::execution::env<> {
        return {};
    }
};

struct outer_stop_receiver {
    using receiver_concept = std::execution::receiver_tag;

    std::inplace_stop_source* source;
    bool* stopped;
    bool* errored;

    template<class... Vs>
    void set_value(Vs&&...) && noexcept {
        *this->errored = true;
    }

    template<class E>
    void set_error(E&&) && noexcept {
        *this->errored = true;
    }

    void set_stopped() && noexcept {
        *this->stopped = true;
    }

    auto get_env() const noexcept {
        return std::execution::env{
            std::execution::prop(std::get_stop_token_t{}, this->source->get_token())};
    }
};

struct member_stop_env {
    std::inplace_stop_token token;

    auto query(std::get_stop_token_t) const noexcept
        -> std::inplace_stop_token {
        return token;
    }
};

struct member_stop_error_receiver {
    using receiver_concept = std::execution::receiver_tag;

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
    using receiver_concept = std::execution::receiver_tag;

    int* value;
    bool* errored;

    void set_value(int result) && noexcept {
        *this->value = result;
    }

    void set_error(std::exception_ptr) && noexcept {
        *this->errored = true;
    }

    auto get_env() const noexcept -> std::execution::env<> {
        return {};
    }
};

template<bool Forwarding>
struct child_query {
    constexpr bool query(std::forwarding_query_t) const noexcept {
        return Forwarding;
    }

    template<class Env>
        requires requires(const Env& env, const child_query& query) { env.query(query); }
    int operator()(const Env& env) const noexcept {
        return env.query(*this);
    }
};

struct member_child_query_env {
    template<bool Forwarding>
    int query(child_query<Forwarding>) const noexcept {
        return 7;
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

template<bool Forwarding, class Env, bool HasMember = true>
void check_child_query_filter() {
    int value = -1;
    bool errored = false;
    using receiver_t = query_env_receiver<Env>;
    static_assert(std::execution::receiver<receiver_t>);

    auto direct_sender = std::execution::read_env(observe_child_query<Forwarding>{});
    using direct_cs_t = decltype(std::execution::get_completion_signatures(
        direct_sender, std::declval<Env>()));
    static_assert(std::is_same_v<direct_cs_t, std::execution::completion_signatures<
        std::execution::set_value_t(std::conditional_t<HasMember, int, bool>)>>);
    auto direct = std::execution::connect(
        std::move(direct_sender), receiver_t{{&value, &errored}, {}});
    std::execution::start(direct);
    EXPECT_EQ(value, HasMember ? 7 : 0);
    EXPECT_FALSE(errored);

    value = -1;
    auto sndr = std::execution::when_all(
        std::execution::read_env(observe_child_query<Forwarding>{}));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::declval<Env>()));
    using result_t = std::conditional_t<Forwarding && HasMember, int, bool>;
    static_assert(std::is_same_v<cs_t, std::execution::completion_signatures<
        std::execution::set_value_t(result_t)>>);
    auto op = std::execution::connect(
        std::move(sndr), receiver_t{{&value, &errored}, {}});
    std::execution::start(op);
    EXPECT_EQ(value, Forwarding && HasMember ? 7 : 0);
    EXPECT_FALSE(errored);
}

} // namespace

TEST(WhenAllTest, AggregatesMultipleValues) {
    auto sndr = std::execution::when_all(
        std::execution::just(1),
        std::execution::just(2.0));
    auto result = std::this_thread::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 1);
    EXPECT_DOUBLE_EQ(std::get<1>(*result), 2.0);
}

TEST(WhenAllTest, SingleSender) {
    auto sndr = std::execution::when_all(std::execution::just(42));
    auto result = std::this_thread::sync_wait(std::move(sndr));
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
    EXPECT_EQ(forge_test::observe_completion<int>(std::move(sndr)).error, 42);
}

TEST(WhenAllTest, RejectsMultipleValueSignaturesFromOneChild) {
    namespace ex = std::execution;
    using invalid = decltype(ex::when_all(multi_value_sender{}, ex::just(1)));
    using valid = decltype(ex::when_all_with_variant(multi_value_sender{}, ex::just(1)));
    static_assert(ex::sender<invalid>);
    static_assert(!ex::sender_in<invalid, ex::env<>>);
    static_assert(ex::sender_in<valid, ex::env<>>);
}

template<bool RejectMutableLvalue = false>
struct category_sensitive_when_all_child {
    using sender_concept = std::execution::sender_tag;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept {
        namespace ex = std::execution;
        if constexpr (std::is_const_v<std::remove_reference_t<Self>>) {
            using value_t = std::conditional_t<std::is_lvalue_reference_v<Self>, long, long long>;
            return ex::completion_signatures<ex::set_value_t(value_t)>{};
        } else if constexpr (!std::is_lvalue_reference_v<Self>) {
            return ex::completion_signatures<ex::set_value_t(short)>{};
        } else if constexpr (RejectMutableLvalue) {
            return ex::completion_signatures<ex::set_value_t(int), ex::set_value_t(double)>{};
        } else {
            return ex::completion_signatures<ex::set_value_t(int)>{};
        }
    }

    template<std::execution::receiver R>
    auto connect(R r) & {
        return std::execution::connect(std::execution::just(1), std::move(r));
    }

    template<std::execution::receiver R>
    auto connect(R r) const& {
        return std::execution::connect(std::execution::just(2L), std::move(r));
    }

    template<std::execution::receiver R>
    auto connect(R r) && {
        return std::execution::connect(std::execution::just(short{3}), std::move(r));
    }

    template<std::execution::receiver R>
    auto connect(R r) const&& {
        return std::execution::connect(std::execution::just(4LL), std::move(r));
    }
};

TEST(WhenAllTest, PreservesChildCategoryInSignaturesStorageAndConnection) {
    namespace ex = std::execution;
    using invalid = decltype(ex::when_all(category_sensitive_when_all_child<true>{}));
    static_assert(!ex::sender_in<invalid&, ex::env<>>);
    static_assert(ex::sender_in<const invalid&, ex::env<>>);

    auto joined = ex::when_all(category_sensitive_when_all_child<>{});
    auto lvalue = std::this_thread::sync_wait(joined);
    auto const_lvalue = std::this_thread::sync_wait(std::as_const(joined));
    auto rvalue = std::this_thread::sync_wait(std::move(joined));
    auto const_rvalue = std::this_thread::sync_wait(std::move(std::as_const(joined)));

    static_assert(std::same_as<decltype(lvalue), std::optional<std::tuple<int>>>);
    static_assert(std::same_as<decltype(const_lvalue), std::optional<std::tuple<long>>>);
    static_assert(std::same_as<decltype(rvalue), std::optional<std::tuple<short>>>);
    static_assert(std::same_as<decltype(const_rvalue), std::optional<std::tuple<long long>>>);
    EXPECT_EQ(lvalue, std::optional{std::tuple{1}});
    EXPECT_EQ(const_lvalue, std::optional{std::tuple{2L}});
    EXPECT_EQ(rvalue, std::optional{std::tuple{short{3}}});
    EXPECT_EQ(const_rvalue, std::optional{std::tuple{4LL}});
}

struct rvalue_only_when_all_child {
    using sender_concept = std::execution::sender_tag;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> { return {}; }

    template<std::execution::receiver R>
    auto connect(R receiver) && {
        return std::execution::connect(std::execution::just(42), std::move(receiver));
    }
};

template<class S>
concept connects_to_value_error_receiver = requires(S&& sender, value_error_receiver receiver) {
    std::execution::connect(std::forward<S>(sender), std::move(receiver));
};

TEST(WhenAllTest, RejectsCategoriesWithoutAChildConnection) {
    namespace ex = std::execution;
    using joined = decltype(ex::when_all(ex::just(), rvalue_only_when_all_child{}));
    static_assert(std::copy_constructible<rvalue_only_when_all_child>);
    static_assert(ex::sender_in<joined&, ex::env<>>);
    static_assert(!connects_to_value_error_receiver<joined&>);
    static_assert(!connects_to_value_error_receiver<const joined&>);
    static_assert(!connects_to_value_error_receiver<const joined&&>);
    static_assert(connects_to_value_error_receiver<joined&&>);
}

TEST(WhenAllTest, WithVariantReportsSingleVariantValuePerChild) {
    auto sndr = std::execution::when_all_with_variant(
        multi_value_sender{},
        other_multi_value_sender{});
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::env<>{}));
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
    auto result = std::this_thread::sync_wait(
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

TEST(WhenAllTest, AdvertisesEmptyValueSignatureWhenAChildCannotProduceValue) {
    auto sndr = std::execution::when_all(
        std::execution::just_error(42),
        std::execution::just(2));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::env<>{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(),
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
    {
        auto op = std::execution::connect(
            std::move(sndr),
            owned_scheduler_receiver{{sch, &got_scheduler, &completed}, &alive});
        EXPECT_EQ(alive, 1);
        std::execution::start(op);
        EXPECT_TRUE(completed);
        EXPECT_TRUE(got_scheduler);
        EXPECT_EQ(alive, 1);
    }
    EXPECT_EQ(alive, 0);
}

TEST(WhenAllTest, ChildEnvFiltersNonforwardingMemberQueries) {
    check_child_query_filter<true, member_child_query_env>();
    check_child_query_filter<false, member_child_query_env>();
}

TEST(WhenAllTest, ChildEnvIgnoresTagOnlyQueries) {
    check_child_query_filter<true, tag_child_query_env, false>();
    check_child_query_filter<false, tag_child_query_env, false>();
}

TEST(WhenAllTest, ChildEnvOverridesOuterStopToken) {
    std::inplace_stop_source source;
    int value = -1;
    bool errored = false;
    auto observes_child_token = [outer = source.get_token()](auto token) noexcept {
        static_assert(std::is_same_v<decltype(token), std::inplace_stop_token>);
        return int(token != outer);
    };
    using receiver_t = query_env_receiver<member_stop_env>;

    auto direct = std::execution::connect(
        (std::execution::read_env(std::get_stop_token)
            | std::execution::then(observes_child_token)),
        receiver_t{{&value, &errored}, {source.get_token()}});
    std::execution::start(direct);
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(errored);

    value = -1;
    auto op = std::execution::connect(
        std::execution::when_all((std::execution::read_env(std::get_stop_token)
            | std::execution::then(observes_child_token))),
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
    EXPECT_TRUE(forge_test::observe_completion(std::move(sndr)).stopped);
}

TEST(WhenAllTest, ErrorOverridesPriorStopped) {
    auto sndr = std::execution::when_all(
        std::execution::just_stopped(),
        std::execution::just_error(42));
    EXPECT_EQ(forge_test::observe_completion<int>(std::move(sndr)).error, 42);
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
