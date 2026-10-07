#pragma once

#include <execution>

#include <type_traits>
#include <utility>

namespace forwarding_attrs_probe {

namespace ex = std::execution;

template<class Tag>
struct query_cpo {
    template<class Env, class... Args>
        requires ex::__forge_env_detail::__queryable<Tag, const Env&, Args...>
    constexpr decltype(auto) operator()(const Env& env, Args&&... args) const
        noexcept(ex::__forge_env_detail::__nothrow_query<Tag, const Env&, Args...>) {
        return ex::__forge_env_detail::__query(
            Tag{}, env, static_cast<Args&&>(args)...);
    }
};

struct value_query : query_cpo<value_query> {
    constexpr bool query(std::forwarding_query_t) const noexcept { return true; }
};

struct reference_query {
    friend constexpr bool tag_invoke(std::forwarding_query_t, reference_query) noexcept {
        return true;
    }

    template<class Env>
        requires ex::__forge_detail::tag_invocable<reference_query, const Env&>
    constexpr decltype(auto) operator()(const Env& env) const
        noexcept(ex::__forge_detail::nothrow_tag_invocable<reference_query, const Env&>) {
        return ex::__forge_detail::tag_invoke_fn(*this, env);
    }
};

struct marked_query : std::forwarding_query_t, query_cpo<marked_query> {
    using query_cpo<marked_query>::operator();
};

struct local_query : query_cpo<local_query> {};

struct blocked_query : std::forwarding_query_t, query_cpo<blocked_query> {
    using query_cpo<blocked_query>::operator();
    constexpr bool query(std::forwarding_query_t) const noexcept { return false; }
    friend constexpr bool tag_invoke(std::forwarding_query_t, blocked_query) noexcept {
        return true;
    }
};

struct argument_query : query_cpo<argument_query> {
    constexpr bool query(std::forwarding_query_t) const noexcept { return true; }
};

struct missing_query : query_cpo<missing_query> {
    constexpr bool query(std::forwarding_query_t) const noexcept { return true; }
};

template<bool Forward>
struct actual_query {
    inline static int default_constructions = 0;

    actual_query() noexcept { ++default_constructions; }
    constexpr explicit actual_query(int) noexcept {}

    constexpr bool query(std::forwarding_query_t) const noexcept { return Forward; }
    friend constexpr bool operator==(actual_query, actual_query) noexcept = default;

    template<class Env, class... Args>
        requires ex::__forge_env_detail::__queryable<actual_query, const Env&, Args...>
    constexpr decltype(auto) operator()(const Env& env, Args&&... args) const
        noexcept(ex::__forge_env_detail::__nothrow_query<actual_query, const Env&, Args...>) {
        return ex::__forge_env_detail::__query(
            *this, env, static_cast<Args&&>(args)...);
    }
};

inline constexpr actual_query<true> actual_forwarding{0};
inline constexpr actual_query<false> actual_nonforwarding{0};

struct argument { int value = 3; };
struct actual_query_failure {};
struct receiver_env {};
struct member_domain {};
struct tag_domain {};
struct domain_failure {};

struct attribute_state {
    int reads = 0;
    int alive = 0;
    int copies = 0;
    int moves = 0;
    int raw_domain_calls = 0;
};

struct attributes {
    attribute_state* state;
    int value = 17;

    explicit attributes(attribute_state& s) noexcept : state(&s) { ++state->alive; }
    attributes(const attributes& other) noexcept : state(other.state), value(other.value) {
        ++state->alive;
        ++state->copies;
    }
    attributes(attributes&& other) noexcept : state(other.state), value(other.value) {
        ++state->alive;
        ++state->moves;
    }
    ~attributes() { --state->alive; }

    int query(value_query) const noexcept { return value; }
    friend int tag_invoke(value_query, const attributes& self) noexcept {
        return self.value + 1000;
    }
    friend const int& tag_invoke(reference_query, const attributes& self) noexcept {
        return self.value;
    }
    int query(marked_query) const noexcept { return value; }
    int query(local_query) const noexcept { return value; }
    friend int tag_invoke(local_query, const attributes& self) noexcept { return self.value; }
    int query(blocked_query) const noexcept { return value; }
    friend int tag_invoke(blocked_query, const attributes& self) noexcept { return self.value; }

    const int& query(actual_query<true>) const noexcept { return value; }
    const int& query(actual_query<false>) const noexcept { return value; }
    friend const int& tag_invoke(actual_query<false>, const attributes& self) noexcept {
        return self.value;
    }
    const int& query(actual_query<true>, argument& arg) const {
        if (arg.value < 0) {
            throw actual_query_failure{};
        }
        return value;
    }

    int query(argument_query, argument& arg, int extra) const noexcept {
        return value + arg.value + extra;
    }
    int query(argument_query, argument&& arg, int extra) const noexcept(false) {
        return value + arg.value + extra + 100;
    }

    member_domain query(ex::get_completion_domain_t<>, const receiver_env&) const {
        ++state->raw_domain_calls;
        throw domain_failure{};
    }
    friend tag_domain tag_invoke(
        ex::get_completion_domain_t<>, const attributes& self, const receiver_env&) noexcept {
        ++self.state->raw_domain_calls;
        return {};
    }
    friend tag_domain tag_invoke(
        ex::get_completion_domain_t<ex::set_value_t>,
        const attributes& self, const receiver_env&) noexcept {
        ++self.state->raw_domain_calls;
        return {};
    }
};

struct immovable_attributes : attributes {
    using attributes::attributes;
    immovable_attributes(const immovable_attributes&) = delete;
    immovable_attributes(immovable_attributes&&) = delete;
};

enum class completion { value, error, stopped };

template<class Attrs = attributes, bool Borrowed = false, bool ConstBorrow = true>
struct source {
    using sender_concept = ex::sender_t;
    attribute_state* state;
    Attrs* borrowed = nullptr;
    completion outcome = completion::value;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(int), ex::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::conditional_t<Borrowed,
        std::conditional_t<ConstBorrow, const Attrs&, Attrs&>, Attrs> {
        ++state->reads;
        if constexpr (Borrowed) {
            return *borrowed;
        } else {
            return Attrs(*state);
        }
    }

    template<ex::receiver R>
    struct operation : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        R receiver;
        completion outcome;

        operation(R r, completion c) : receiver(std::move(r)), outcome(c) {}
        void start() & noexcept {
            if (outcome == completion::error) {
                ex::set_error(std::move(receiver), 11);
            } else if (outcome == completion::stopped) {
                ex::set_stopped(std::move(receiver));
            } else {
                ex::set_value(std::move(receiver), 7);
            }
        }
    };

    template<ex::receiver R>
    auto connect(R r) const -> operation<R> {
        return operation<R>{std::move(r), outcome};
    }
};

struct value_callback {
    template<class... Args>
    int operator()(Args&&...) const noexcept { return 42; }
};

struct sender_callback {
    template<class... Args>
    auto operator()(Args&&...) const noexcept { return ex::just(42); }
};

struct bulk_callback {
    template<class... Args>
    void operator()(Args&&...) const noexcept {}
};

template<class S, class Verify>
void for_each_adaptor(S sender, Verify verify) {
    verify(ex::then(sender, value_callback{}));
    verify(ex::upon_error(sender, value_callback{}));
    verify(ex::upon_stopped(sender, value_callback{}));
    verify(ex::let_value(sender, sender_callback{}));
    verify(ex::let_error(sender, sender_callback{}));
    verify(ex::let_stopped(sender, sender_callback{}));
    verify(ex::bulk(sender, 3, bulk_callback{}));
    verify(ex::bulk_unchunked(sender, 3, bulk_callback{}));
    verify(ex::bulk_chunked(sender, 3, bulk_callback{}));
    verify(ex::into_variant(sender));
    verify(ex::stopped_as_optional(sender));
    verify(ex::stopped_as_error(sender, 55));
}

} // namespace forwarding_attrs_probe
