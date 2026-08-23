#include <gtest/gtest.h>
#include <execution>
#include <cstddef>
#include <concepts>
#include <type_traits>
#include <utility>

namespace {

struct domain_probe_query_t {
    template<class Env>
    auto operator()(const Env& env) const noexcept
        -> decltype(tag_invoke(*this, env)) {
        return tag_invoke(*this, env);
    }
};

inline constexpr domain_probe_query_t domain_probe_query{};

struct transformed_env_query_t {
    template<class Env>
    auto operator()(const Env& env) const noexcept
        -> decltype(tag_invoke(*this, env)) {
        return tag_invoke(*this, env);
    }
};

inline constexpr transformed_env_query_t transformed_env_query{};

template<class Env = std::execution::empty_env>
struct int_receiver {
    using receiver_concept = std::execution::receiver_t;

    int* value = nullptr;
    bool* completed = nullptr;
    Env env{};

    void set_value(int v) && noexcept {
        if (value) *value = v;
        if (completed) *completed = true;
    }

    template<class E>
    void set_error(E&&) && noexcept {}

    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> Env {
        return env;
    }
};

template<class R>
struct int_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;
    int value;

    int_op(R r, int v) : rcvr(std::move(r)), value(v) {}
    int_op(const int_op&) = delete;
    int_op& operator=(const int_op&) = delete;

    void start() & noexcept {
        std::execution::set_value(std::move(rcvr), value);
    }
};

struct direct_value_sender {
    using sender_concept = std::execution::sender_t;

    int value = 42;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R r) && -> int_op<R> {
        return int_op<R>{std::move(r), value};
    }

    template<std::execution::receiver R>
    auto connect(R r) const& -> int_op<R> {
        return int_op<R>{std::move(r), value};
    }
};

struct tracking_domain {
    static inline bool transformed = false;

    template<class Sender, class Env>
    Sender&& transform_sender(Sender&& sndr, const Env&) const noexcept {
        transformed = true;
        return static_cast<Sender&&>(sndr);
    }
};

struct queried_domain {
    int identity = 17;
};

struct runtime_domain_query_env {
    static inline int calls = 0;

    auto query(std::execution::get_domain_t) const -> queried_domain {
        ++calls;
        throw 42;
    }
};

struct runtime_completion_domain_attrs {
    static inline int calls = 0;

    template<class Env>
    auto query(
        std::execution::get_completion_domain_t<>,
        const Env&) const -> queried_domain {
        ++calls;
        throw 42;
    }
};

struct non_default_domain {
    explicit non_default_domain(int) noexcept {}
};

struct non_default_domain_env {
    auto query(std::execution::get_domain_t) const noexcept
        -> non_default_domain;
};

struct throwing_default_domain {
    throwing_default_domain() noexcept(false) {}
};

struct throwing_default_completion_attrs {
    template<class Env>
    auto query(
        std::execution::get_completion_domain_t<>,
        const Env&) const noexcept -> throwing_default_domain;
};

template<class Env>
concept has_domain_query = requires(const Env& env) {
    std::execution::get_domain(env);
};

template<class Attrs>
concept has_completion_domain_query = requires(const Attrs& attrs) {
    std::execution::get_completion_domain<>(
        attrs, std::execution::empty_env{});
};

static_assert(!has_domain_query<non_default_domain_env>);
static_assert(!has_completion_domain_query<throwing_default_completion_attrs>);

struct receiver_domain_env {
    friend auto tag_invoke(std::execution::get_domain_t, const receiver_domain_env&) noexcept
        -> tracking_domain {
        return {};
    }
};

struct sender_tracking_domain {
    static inline bool transformed = false;

    template<class Sender, class Env>
    Sender&& transform_sender(Sender&& sndr, const Env&) const noexcept {
        transformed = true;
        return static_cast<Sender&&>(sndr);
    }
};

struct sender_domain_env {
    friend auto tag_invoke(std::execution::get_domain_t, const sender_domain_env&) noexcept
        -> sender_tracking_domain {
        return {};
    }
};

struct sender_domain_sender : direct_value_sender {
    auto get_env() const noexcept -> sender_domain_env {
        return {};
    }
};

struct transform_only_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct transformed_sender : direct_value_sender {
    explicit transformed_sender(int v) noexcept {
        value = v;
    }
};

struct signature_source_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct signature_transformed_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(double)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct signature_transform_domain {
    template<class Env>
    auto transform_sender(std::execution::start_t, signature_source_sender&&, const Env&) const noexcept
        -> signature_transformed_sender {
        return {};
    }
};

struct signature_domain_env {
    friend auto tag_invoke(std::execution::get_domain_t, const signature_domain_env&) noexcept
        -> signature_transform_domain {
        return {};
    }
};

struct rawless_signature_source_sender {
    using sender_concept = std::execution::sender_t;

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct rawless_signature_transformed_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(double)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct rawless_signature_transform_domain {
    template<class Env>
    auto transform_sender(
        std::execution::start_t,
        rawless_signature_source_sender&&,
        const Env&) const noexcept
            -> rawless_signature_transformed_sender {
        return {};
    }
};

struct rawless_signature_domain_env {
    friend auto tag_invoke(
        std::execution::get_domain_t,
        const rawless_signature_domain_env&) noexcept
            -> rawless_signature_transform_domain {
        return {};
    }
};

struct explicit_empty_signature_source;

struct explicit_empty_signature_domain {
    template<class Env>
    auto transform_sender(
        std::execution::set_value_t,
        explicit_empty_signature_source&&,
        const Env&) const noexcept -> signature_transformed_sender {
        return {};
    }
};

struct explicit_empty_signature_attributes {
    template<class Env>
    friend auto tag_invoke(
        std::execution::get_completion_domain_t<>,
        const explicit_empty_signature_attributes&,
        const Env&) noexcept -> explicit_empty_signature_domain {
        return {};
    }
};

struct explicit_empty_signature_source {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> explicit_empty_signature_attributes {
        return {};
    }
};

struct rescue_domain {
    static inline bool transformed = false;

    template<class Sender, class Env>
    auto transform_sender(Sender&&, const Env&) const noexcept -> transformed_sender {
        transformed = true;
        return transformed_sender{77};
    }
};

struct rescue_domain_env {
    friend auto tag_invoke(std::execution::get_domain_t, const rescue_domain_env&) noexcept
        -> rescue_domain {
        return {};
    }
};

struct receiver_env_probe_domain {
    static inline int observed = 0;

    template<class Sender, class Env>
    Sender&& transform_sender(Sender&& sndr, const Env& env) const noexcept {
        observed = domain_probe_query(env);
        return static_cast<Sender&&>(sndr);
    }
};

struct receiver_env_probe {
    int marker = 0;

    friend auto tag_invoke(std::execution::get_domain_t, const receiver_env_probe&) noexcept
        -> receiver_env_probe_domain {
        return {};
    }

    friend int tag_invoke(domain_probe_query_t, const receiver_env_probe& self) noexcept {
        return self.marker;
    }
};

struct injected_env {
    int value = 0;

    friend int tag_invoke(transformed_env_query_t, const injected_env& self) noexcept {
        return self.value;
    }
};

struct env_observing_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;

        void start() & noexcept {
            auto value = transformed_env_query(std::execution::get_env(rcvr));
            std::execution::set_value(std::move(rcvr), value);
        }
    };

    template<std::execution::receiver R>
    auto connect(R r) && -> op<R> {
        return op<R>{std::move(r)};
    }
};

struct env_transform_domain {
    template<class Env>
    auto transform_sender(std::execution::start_t, direct_value_sender&&, const Env&) const noexcept {
        return std::execution::write_env(env_observing_sender{}, injected_env{123});
    }
};

struct env_transform_domain_env {
    friend auto tag_invoke(std::execution::get_domain_t, const env_transform_domain_env&) noexcept
        -> env_transform_domain {
        return {};
    }
};

struct late_domain_scheduler {
    using scheduler_concept = std::execution::scheduler_t;

    bool operator==(const late_domain_scheduler&) const noexcept = default;

    auto schedule() const noexcept {
        return std::execution::just();
    }
};

struct scheduler_domain {
    static inline bool transformed = false;

    template<class Sender, class Env>
    Sender&& transform_sender(Sender&& sndr, const Env&) const noexcept {
        transformed = true;
        return static_cast<Sender&&>(sndr);
    }
};

struct scheduler_domain_env {
    friend auto tag_invoke(std::execution::get_scheduler_t, const scheduler_domain_env&) noexcept
        -> late_domain_scheduler {
        return {};
    }

    friend auto tag_invoke(std::execution::get_completion_domain_t<std::execution::set_value_t>,
                           late_domain_scheduler, const scheduler_domain_env&) noexcept
        -> scheduler_domain {
        return {};
    }
};

struct completion_domain_two_env;

struct second_completion_domain {
    static inline bool transformed = false;

    template<class Sender, class Env>
    auto transform_sender(std::execution::set_value_t, Sender&&, const Env&) const noexcept
        -> transformed_sender {
        transformed = true;
        return transformed_sender{88};
    }
};

struct completion_domain_two_env {
    template<class Env>
    friend auto tag_invoke(std::execution::get_completion_domain_t<>,
                           const completion_domain_two_env&, const Env&) noexcept
        -> second_completion_domain {
        return {};
    }
};

struct completion_mid_sender : direct_value_sender {
    auto get_env() const noexcept -> completion_domain_two_env {
        return {};
    }
};

struct first_completion_domain {
    static inline bool transformed = false;

    template<class Sender, class Env>
    auto transform_sender(std::execution::set_value_t, Sender&&, const Env&) const noexcept
        -> completion_mid_sender {
        transformed = true;
        completion_mid_sender sndr{};
        sndr.value = 66;
        return sndr;
    }
};

struct completion_domain_one_env {
    template<class Env>
    friend auto tag_invoke(std::execution::get_completion_domain_t<>,
                           const completion_domain_one_env&, const Env&) noexcept
        -> first_completion_domain {
        return {};
    }
};

struct completion_domain_source : direct_value_sender {
    auto get_env() const noexcept -> completion_domain_one_env {
        return {};
    }
};

struct start_mid_sender : direct_value_sender {};

struct start_seed_sender : direct_value_sender {};

struct recursive_start_domain {
    static inline bool first = false;
    static inline bool second = false;

    template<class Env>
    auto transform_sender(std::execution::start_t, start_seed_sender&&, const Env&) const noexcept
        -> start_mid_sender {
        first = true;
        start_mid_sender sndr{};
        sndr.value = 71;
        return sndr;
    }

    template<class Env>
    auto transform_sender(std::execution::start_t, start_mid_sender&&, const Env&) const noexcept
        -> transformed_sender {
        second = true;
        return transformed_sender{99};
    }
};

struct recursive_start_domain_env {
    friend auto tag_invoke(std::execution::get_domain_t, const recursive_start_domain_env&) noexcept
        -> recursive_start_domain {
        return {};
    }
};

struct exact_receiver {
    using receiver_concept = std::execution::receiver_t;

    bool* completed = nullptr;

    void set_value(int) && noexcept {
        if (completed) *completed = true;
    }

    template<class E>
    void set_error(E&&) && noexcept {}

    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct exact_receiver_sender : direct_value_sender {
    template<class R>
        requires std::same_as<std::remove_cvref_t<R>, exact_receiver>
    auto connect(R r) && -> int_op<R> {
        return int_op<R>{std::move(r), value};
    }
};

struct public_transform_tag {
    static inline bool transformed = false;

    template<class Sender, class Env>
    auto transform_sender(
        std::execution::set_value_t,
        Sender&&,
        const Env&) const noexcept -> transformed_sender {
        transformed = true;
        return transformed_sender{121};
    }
};

struct sender_tag_transform_source : direct_value_sender {
    template<std::size_t I>
    auto get() const noexcept -> public_transform_tag {
        static_assert(I == 0);
        return {};
    }
};

struct apply_probe_tag {
    static inline int tag_calls = 0;

    template<class Sender>
    int apply_sender(Sender&& sndr, int offset) const noexcept {
        ++tag_calls;
        return sndr.value + offset;
    }
};

struct apply_probe_domain {
    static inline int domain_calls = 0;

    template<class Sender>
    long apply_sender(apply_probe_tag, Sender&& sndr, int offset) const noexcept {
        ++domain_calls;
        return static_cast<long>(sndr.value + offset + 1000);
    }
};

struct unavailable_apply_tag {};

struct throwing_apply_tag {
    template<class Sender>
    int apply_sender(Sender&&, int) const noexcept(false) {
        return 0;
    }
};

struct apply_category_tag {
    int apply_sender(direct_value_sender&, int) const noexcept {
        return 1;
    }

    int apply_sender(direct_value_sender&&, int) const noexcept {
        return 2;
    }
};

template<class Domain, class Tag, class Sender>
concept can_apply_sender = requires(Domain domain, Tag tag, Sender&& sndr) {
    std::execution::apply_sender(
        domain, tag, static_cast<Sender&&>(sndr), 3);
};

} // namespace

// Domain dispatch tests

TEST(DefaultDomainTest, GetDomainFromEmptyEnv) {
    std::execution::empty_env env{};
    auto domain = std::execution::get_domain(env);
    static_assert(std::is_same_v<decltype(domain), std::execution::default_domain>);
    SUCCEED();
}

TEST(DefaultDomainTest, QuerySelectsTypeWithoutEvaluatingQueryValue) {
    runtime_domain_query_env::calls = 0;

    auto domain = std::execution::get_domain(runtime_domain_query_env{});

    static_assert(noexcept(std::execution::get_domain(
        std::declval<const runtime_domain_query_env&>())));
    EXPECT_EQ(runtime_domain_query_env::calls, 0);
    EXPECT_EQ(domain.identity, 17);
}

TEST(DefaultDomainTest, CompletionQuerySelectsTypeWithoutEvaluatingQueryValue) {
    runtime_completion_domain_attrs::calls = 0;

    auto domain = std::execution::get_completion_domain<>(
        runtime_completion_domain_attrs{}, std::execution::empty_env{});

    static_assert(noexcept(std::execution::get_completion_domain<>(
        std::declval<const runtime_completion_domain_attrs&>(),
        std::execution::empty_env{})));
    EXPECT_EQ(runtime_completion_domain_attrs::calls, 0);
    EXPECT_EQ(domain.identity, 17);
}

TEST(DefaultDomainTest, TransformSenderIsIdentity) {
    auto sndr = std::execution::just(42);
    std::execution::empty_env env{};
    std::execution::default_domain domain{};
    auto& result = domain.transform_sender(sndr, env);
    static_assert(std::same_as<
        decltype(std::execution::transform_sender(sndr, env)),
        decltype(sndr)&>);
    static_assert(noexcept(std::execution::transform_sender(sndr, env)));
    (void)result;
    SUCCEED();
}

TEST(DefaultDomainTest, PublicTransformUsesSenderTagCustomization) {
    public_transform_tag::transformed = false;

    auto transformed = std::execution::transform_sender(
        sender_tag_transform_source{}, std::execution::empty_env{});

    static_assert(std::same_as<decltype(transformed), transformed_sender>);
    EXPECT_TRUE(public_transform_tag::transformed);
    EXPECT_EQ(transformed.value, 121);
}

TEST(DefaultDomainTest, PublicTransformRunsCompletionThenStartRecursion) {
    first_completion_domain::transformed = false;
    second_completion_domain::transformed = false;
    recursive_start_domain::first = false;
    recursive_start_domain::second = false;

    auto completion_transformed = std::execution::transform_sender(
        completion_domain_source{}, std::execution::empty_env{});
    auto start_transformed = std::execution::transform_sender(
        start_seed_sender{}, recursive_start_domain_env{});

    static_assert(std::same_as<
        decltype(completion_transformed), transformed_sender>);
    static_assert(std::same_as<
        decltype(start_transformed), transformed_sender>);
    EXPECT_TRUE(first_completion_domain::transformed);
    EXPECT_TRUE(second_completion_domain::transformed);
    EXPECT_EQ(completion_transformed.value, 88);
    EXPECT_TRUE(recursive_start_domain::first);
    EXPECT_TRUE(recursive_start_domain::second);
    EXPECT_EQ(start_transformed.value, 99);
}

TEST(DefaultDomainTest, PublicTransformCanRescueRawSourceSender) {
    auto transformed = std::execution::transform_sender(
        rawless_signature_source_sender{}, rawless_signature_domain_env{});

    static_assert(std::same_as<
        decltype(transformed), rawless_signature_transformed_sender>);
}

TEST(DefaultDomainTest, ApplySenderPrefersExplicitDomain) {
    apply_probe_tag::tag_calls = 0;
    apply_probe_domain::domain_calls = 0;

    auto result = std::execution::apply_sender(
        apply_probe_domain{}, apply_probe_tag{}, direct_value_sender{7}, 5);

    static_assert(std::same_as<decltype(result), long>);
    static_assert(noexcept(std::execution::apply_sender(
        apply_probe_domain{}, apply_probe_tag{}, direct_value_sender{}, 1)));
    EXPECT_EQ(result, 1012);
    EXPECT_EQ(apply_probe_domain::domain_calls, 1);
    EXPECT_EQ(apply_probe_tag::tag_calls, 0);
}

TEST(DefaultDomainTest, ApplySenderFallsBackToTagCustomization) {
    apply_probe_tag::tag_calls = 0;

    auto result = std::execution::apply_sender(
        std::execution::default_domain{},
        apply_probe_tag{},
        direct_value_sender{9},
        4);

    static_assert(std::same_as<decltype(result), int>);
    static_assert(!noexcept(std::execution::apply_sender(
        std::execution::default_domain{},
        throwing_apply_tag{},
        direct_value_sender{},
        1)));
    static_assert(!can_apply_sender<
        std::execution::default_domain,
        unavailable_apply_tag,
        direct_value_sender>);
    EXPECT_EQ(result, 13);
    EXPECT_EQ(apply_probe_tag::tag_calls, 1);
}

TEST(DefaultDomainTest, ApplySenderPreservesSenderValueCategory) {
    direct_value_sender sndr{};

    EXPECT_EQ(std::execution::apply_sender(
        std::execution::default_domain{}, apply_category_tag{}, sndr, 0), 1);
    EXPECT_EQ(std::execution::apply_sender(
        std::execution::default_domain{},
        apply_category_tag{},
        direct_value_sender{},
        0), 2);
}

TEST(DefaultDomainTest, DefaultDomainFastPathKeepsReceiverType) {
    bool completed = false;

    auto op = std::execution::connect(exact_receiver_sender{}, exact_receiver{&completed});
    std::execution::start(op);

    EXPECT_TRUE(completed);
}

TEST(DefaultDomainTest, ConnectUsesReceiverEnvDomainTransform) {
    tracking_domain::transformed = false;
    int value = 0;
    bool completed = false;

    auto op = std::execution::connect(
        direct_value_sender{42},
        int_receiver<receiver_domain_env>{&value, &completed, receiver_domain_env{}});
    std::execution::start(op);

    EXPECT_TRUE(tracking_domain::transformed);
    EXPECT_TRUE(completed);
    EXPECT_EQ(value, 42);
}

TEST(DefaultDomainTest, ReceiverDomainBeatsSenderEnvDomain) {
    tracking_domain::transformed = false;
    sender_tracking_domain::transformed = false;
    int value = 0;
    bool completed = false;

    auto op = std::execution::connect(
        sender_domain_sender{},
        int_receiver<receiver_domain_env>{&value, &completed, receiver_domain_env{}});
    std::execution::start(op);

    EXPECT_TRUE(tracking_domain::transformed);
    EXPECT_FALSE(sender_tracking_domain::transformed);
    EXPECT_TRUE(completed);
    EXPECT_EQ(value, 42);
}

TEST(DefaultDomainTest, SenderEnvDomainIgnoredWhenReceiverUsesDefaultDomain) {
    sender_tracking_domain::transformed = false;
    int value = 0;
    bool completed = false;

    auto op = std::execution::connect(
        sender_domain_sender{},
        int_receiver<>{&value, &completed, {}});
    std::execution::start(op);

    EXPECT_FALSE(sender_tracking_domain::transformed);
    EXPECT_TRUE(completed);
    EXPECT_EQ(value, 42);
}

TEST(DefaultDomainTest, TransformSenderCanMakeSenderConnectable) {
    rescue_domain::transformed = false;
    int value = 0;
    bool completed = false;

    auto op = std::execution::connect(
        transform_only_sender{},
        int_receiver<rescue_domain_env>{&value, &completed, rescue_domain_env{}});
    std::execution::start(op);

    EXPECT_TRUE(rescue_domain::transformed);
    EXPECT_TRUE(completed);
    EXPECT_EQ(value, 77);
}

TEST(DefaultDomainTest, TransformSenderReceivesReceiverEnv) {
    receiver_env_probe_domain::observed = 0;
    int value = 0;
    bool completed = false;

    auto op = std::execution::connect(
        direct_value_sender{7},
        int_receiver<receiver_env_probe>{&value, &completed, receiver_env_probe{314}});
    std::execution::start(op);

    EXPECT_EQ(receiver_env_probe_domain::observed, 314);
    EXPECT_TRUE(completed);
    EXPECT_EQ(value, 7);
}

TEST(DefaultDomainTest, DomainCanInjectEnvWithWriteEnv) {
    int value = 0;
    bool completed = false;

    auto op = std::execution::connect(
        direct_value_sender{},
        int_receiver<env_transform_domain_env>{&value, &completed, env_transform_domain_env{}});
    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_EQ(value, 123);
}

TEST(DefaultDomainTest, GetCompletionSignaturesUsesTransformedSender) {
    using cs_t = decltype(std::execution::get_completion_signatures(
        signature_source_sender{},
        signature_domain_env{}));
    static_assert(std::is_same_v<
        cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(double)>>);
}

TEST(DefaultDomainTest, GetCompletionSignaturesAllowsRawlessSourceAfterTransform) {
    using cs_t = decltype(std::execution::get_completion_signatures(
        rawless_signature_source_sender{},
        rawless_signature_domain_env{}));
    static_assert(std::is_same_v<
        cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(double)>>);
}

TEST(DefaultDomainTest, ExplicitEmptyEnvUsesCompletionDomainTransform) {
    using raw_cs_t = std::execution::completion_signatures_of_t<
        explicit_empty_signature_source>;
    using transformed_cs_t = std::execution::completion_signatures_of_t<
        explicit_empty_signature_source,
        std::execution::empty_env>;

    static_assert(std::is_same_v<
        raw_cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int)>>);
    static_assert(std::is_same_v<
        transformed_cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(double)>>);
}

TEST(DefaultDomainTest, InvalidTransformedSenderFailsSenderInConstraint) {
    static_assert(!std::execution::sender_in<
        rawless_signature_source_sender,
        receiver_domain_env>);
}

TEST(DefaultDomainTest, CompletionDomainRecursesBeforeStartDomain) {
    first_completion_domain::transformed = false;
    second_completion_domain::transformed = false;

    int value = 0;
    bool completed = false;

    auto op = std::execution::connect(
        completion_domain_source{},
        int_receiver<>{&value, &completed, {}});
    std::execution::start(op);

    EXPECT_TRUE(first_completion_domain::transformed);
    EXPECT_TRUE(second_completion_domain::transformed);
    EXPECT_TRUE(completed);
    EXPECT_EQ(value, 88);
}

TEST(DefaultDomainTest, StartDomainRecursesUntilTypeStabilizes) {
    recursive_start_domain::first = false;
    recursive_start_domain::second = false;

    int value = 0;
    bool completed = false;

    auto op = std::execution::connect(
        start_seed_sender{},
        int_receiver<recursive_start_domain_env>{&value, &completed, recursive_start_domain_env{}});
    std::execution::start(op);

    EXPECT_TRUE(recursive_start_domain::first);
    EXPECT_TRUE(recursive_start_domain::second);
    EXPECT_TRUE(completed);
    EXPECT_EQ(value, 99);
}

TEST(DefaultDomainTest, GetDomainCanUseReceiverSchedulerDomain) {
    scheduler_domain_env env{};
    auto domain = std::execution::get_domain(env);
    static_assert(std::is_same_v<decltype(domain), scheduler_domain>);
}

TEST(DefaultDomainTest, ConnectUsesReceiverSchedulerDomain) {
    scheduler_domain::transformed = false;
    int value = 0;
    bool completed = false;

    auto op = std::execution::connect(
        direct_value_sender{55},
        int_receiver<scheduler_domain_env>{&value, &completed, scheduler_domain_env{}});
    std::execution::start(op);

    EXPECT_TRUE(scheduler_domain::transformed);
    EXPECT_TRUE(completed);
    EXPECT_EQ(value, 55);
}
