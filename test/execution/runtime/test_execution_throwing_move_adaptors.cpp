#include <gtest/gtest.h>

#include <execution>

#include <exception>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

namespace {

namespace ex = std::execution;

enum class adaptor_kind {
    then, upon_error, upon_stopped, bulk, bulk_unchunked, bulk_chunked,
    stopped_as_error
};

enum class completion_kind { value, error, stopped };

struct move_failure {};
struct connect_failure {};
struct invocation_failure {};

struct probe {
    int moves_before_throw = -1;
    bool throw_on_connect = false;
    bool throw_on_call = false;
    int live_payloads = 0;
    int copies = 0;
    int moves = 0;
    int calls = 0;
    int connects = 0;
    int starts = 0;
    int inner_destructions = 0;
    int values = 0;
    int errors = 0;
    int stopped = 0;
    int result = 0;
    std::exception_ptr exception;
    const void* latest_payload = nullptr;
    const void* connected_payload = nullptr;
    const void* connected_env = nullptr;
    bool payload_alive = false;
    bool outer_alive = false;
    bool inner_saw_live_payload = false;
    bool inner_saw_live_outer = false;
    bool inner_saw_same_env = false;
    void* self_destroy_op = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
};

struct tracked_payload {
    probe* state;
    int value = 42;

    tracked_payload* operator&() = delete;
    const tracked_payload* operator&() const = delete;

    explicit tracked_payload(probe& p) : state(&p) {
        ++state->live_payloads;
        state->latest_payload = this;
    }

    tracked_payload(const tracked_payload& other)
        : state(other.state), value(other.value) {
        ++state->copies;
        ++state->live_payloads;
        state->latest_payload = this;
    }

    tracked_payload(tracked_payload&& other) noexcept(false)
        : state(other.state), value(other.value) {
        ++state->moves;
        if (state->moves_before_throw == 0) {
            throw move_failure{};
        }
        if (state->moves_before_throw > 0) {
            --state->moves_before_throw;
        }
        ++state->live_payloads;
        state->latest_payload = this;
    }

    ~tracked_payload() {
        --state->live_payloads;
        if (state->connected_payload == this) {
            state->payload_alive = false;
        }
    }
};

template<adaptor_kind Kind, bool Nothrow = false>
struct payload : tracked_payload {
    using tracked_payload::tracked_payload;

    void invoke() noexcept(Nothrow) {
        ++state->calls;
        if constexpr (!Nothrow) {
            if (state->throw_on_call) {
                throw invocation_failure{};
            }
        }
    }

    int operator()(int v) && noexcept(Nothrow)
        requires (Kind == adaptor_kind::then || Kind == adaptor_kind::upon_error) {
        invoke();
        return v + 1;
    }

    int operator()() && noexcept(Nothrow)
        requires (Kind == adaptor_kind::upon_stopped) {
        invoke();
        return value;
    }

    void operator()(int index, int& v) & noexcept(Nothrow)
        requires (Kind == adaptor_kind::bulk || Kind == adaptor_kind::bulk_unchunked) {
        invoke();
        v += index;
    }

    void operator()(int first, int last, int& v) & noexcept(Nothrow)
        requires (Kind == adaptor_kind::bulk_chunked) {
        invoke();
        v += last - first;
    }
};

struct receiver_env {
    probe* state;
};

struct observing_receiver {
    using receiver_concept = ex::receiver_t;
    receiver_env env;

    explicit observing_receiver(receiver_env e) noexcept : env(e) {}
    observing_receiver(const observing_receiver&) = default;
    observing_receiver(observing_receiver&&) = default;

    observing_receiver* operator&() = delete;
    const observing_receiver* operator&() const = delete;

    ~observing_receiver() {
        if (env.state->connected_env == &env) {
            env.state->outer_alive = false;
        }
    }

    void finish() noexcept {
        auto* state = env.state;
        if (state->self_destroy_op) {
            auto* op = std::exchange(state->self_destroy_op, nullptr);
            state->destroy(op);
        }
    }

    void set_value(int value) && noexcept {
        ++env.state->values;
        env.state->result = value;
        finish();
    }

    void set_error(std::exception_ptr error) && noexcept {
        ++env.state->errors;
        env.state->exception = std::move(error);
        finish();
    }

    void set_error(int error) && noexcept {
        ++env.state->errors;
        env.state->result = error;
        finish();
    }

    template<adaptor_kind Kind, bool Nothrow>
    void set_error(payload<Kind, Nothrow>&& error) && noexcept {
        ++env.state->errors;
        env.state->result = error.value;
        finish();
    }

    void set_stopped() && noexcept {
        ++env.state->stopped;
        finish();
    }

    auto get_env() const noexcept -> const receiver_env& { return env; }
};

static_assert(ex::receiver<observing_receiver>);

template<completion_kind Completion>
struct synchronous_sender {
    using sender_concept = ex::sender_t;
    probe* state;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        if constexpr (Completion == completion_kind::value) {
            return ex::completion_signatures<ex::set_value_t(int)>{};
        } else if constexpr (Completion == completion_kind::error) {
            return ex::completion_signatures<ex::set_error_t(int)>{};
        } else {
            return ex::completion_signatures<ex::set_stopped_t()>{};
        }
    }

    template<ex::receiver R>
    struct op : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        probe* state;
        R rcvr;

        op(probe* p, R r) : state(p), rcvr(std::move(r)) {}

        ~op() {
            ++state->inner_destructions;
            state->inner_saw_live_payload = state->payload_alive;
            state->inner_saw_live_outer = state->outer_alive;
            if (state->outer_alive) {
                state->inner_saw_same_env = &ex::get_env(rcvr) == state->connected_env;
            }
        }

        void start() & noexcept {
            ++state->starts;
            if constexpr (Completion == completion_kind::value) {
                int value = 41;
                ex::set_value(std::move(rcvr), std::move(value));
            } else if constexpr (Completion == completion_kind::error) {
                ex::set_error(std::move(rcvr), 41);
            } else {
                ex::set_stopped(std::move(rcvr));
            }
        }
    };

    template<ex::receiver R>
    auto connect(R r) const -> op<R> {
        static_assert(std::is_nothrow_move_constructible_v<R>);
        static_assert(std::is_same_v<ex::env_of_t<R>, const receiver_env&>);
        static_assert(noexcept(ex::get_env(r)));
        const auto& env = ex::get_env(r);
        EXPECT_EQ(env.state, state);
        ++state->connects;
        state->connected_payload = state->latest_payload;
        state->connected_env = &env;
        state->payload_alive = true;
        state->outer_alive = true;
        if (state->throw_on_connect) {
            throw connect_failure{};
        }
        return op<R>{state, std::move(r)};
    }

    auto get_env() const noexcept -> ex::empty_env { return {}; }
};

template<adaptor_kind Kind, bool Nothrow = false, class S>
auto adapt(S sndr, probe& state) {
    payload<Kind, Nothrow> fn{state};
    static_assert(std::is_copy_constructible_v<decltype(fn)>);
    static_assert(!std::is_nothrow_move_constructible_v<decltype(fn)>);
    if constexpr (Kind == adaptor_kind::then) {
        return ex::then(std::move(sndr), std::move(fn));
    } else if constexpr (Kind == adaptor_kind::upon_error) {
        return ex::upon_error(std::move(sndr), std::move(fn));
    } else if constexpr (Kind == adaptor_kind::upon_stopped) {
        return ex::upon_stopped(std::move(sndr), std::move(fn));
    } else if constexpr (Kind == adaptor_kind::bulk) {
        return ex::bulk(std::move(sndr), 3, std::move(fn));
    } else if constexpr (Kind == adaptor_kind::bulk_unchunked) {
        return ex::bulk_unchunked(std::move(sndr), 3, std::move(fn));
    } else if constexpr (Kind == adaptor_kind::bulk_chunked) {
        return ex::bulk_chunked(std::move(sndr), 3, std::move(fn));
    } else {
        return ex::stopped_as_error(std::move(sndr), std::move(fn));
    }
}

template<adaptor_kind Kind, bool Nothrow = false>
auto make_sender(probe& state) {
    constexpr auto completion = Kind == adaptor_kind::upon_error
        ? completion_kind::error
        : (Kind == adaptor_kind::upon_stopped || Kind == adaptor_kind::stopped_as_error)
            ? completion_kind::stopped : completion_kind::value;
    return adapt<Kind, Nothrow>(synchronous_sender<completion>{&state}, state);
}

template<adaptor_kind Kind>
void expect_success(const probe& state) {
    constexpr bool is_bulk = Kind == adaptor_kind::bulk ||
        Kind == adaptor_kind::bulk_unchunked || Kind == adaptor_kind::bulk_chunked;
    constexpr bool is_error = Kind == adaptor_kind::stopped_as_error;
    EXPECT_EQ(state.values, is_error ? 0 : 1);
    EXPECT_EQ(state.errors, is_error ? 1 : 0);
    EXPECT_EQ(state.stopped, 0);
    EXPECT_EQ(state.result, is_bulk ? 44 : 42);
    constexpr int expected_calls = is_error ? 0 :
        (Kind == adaptor_kind::bulk || Kind == adaptor_kind::bulk_unchunked) ? 3 : 1;
    EXPECT_EQ(state.calls, expected_calls);
    EXPECT_FALSE(state.exception);
}

void expect_inner_first(const probe& state) {
    EXPECT_EQ(state.inner_destructions, 1);
    EXPECT_TRUE(state.inner_saw_live_payload);
    EXPECT_TRUE(state.inner_saw_live_outer);
    EXPECT_TRUE(state.inner_saw_same_env);
    EXPECT_FALSE(state.payload_alive);
    EXPECT_FALSE(state.outer_alive);
}

template<class T>
class ThrowingMoveAdaptors : public ::testing::Test {};

using adaptor_cases = ::testing::Types<
    std::integral_constant<adaptor_kind, adaptor_kind::then>,
    std::integral_constant<adaptor_kind, adaptor_kind::upon_error>,
    std::integral_constant<adaptor_kind, adaptor_kind::upon_stopped>,
    std::integral_constant<adaptor_kind, adaptor_kind::bulk>,
    std::integral_constant<adaptor_kind, adaptor_kind::bulk_unchunked>,
    std::integral_constant<adaptor_kind, adaptor_kind::bulk_chunked>,
    std::integral_constant<adaptor_kind, adaptor_kind::stopped_as_error>>;
TYPED_TEST_SUITE(ThrowingMoveAdaptors, adaptor_cases);

TYPED_TEST(ThrowingMoveAdaptors, RvalueAndCopyableLvalueConnect) {
    constexpr auto kind = TypeParam::value;
    for (bool copy_lvalue : {false, true}) {
        SCOPED_TRACE(copy_lvalue);
        probe state;
        {
            auto sndr = make_sender<kind, true>(state);
            using cs = ex::completion_signatures_of_t<decltype(sndr), ex::empty_env>;
            if constexpr (kind == adaptor_kind::stopped_as_error) {
                static_assert(std::is_same_v<cs, ex::completion_signatures<
                    ex::set_error_t(payload<kind, true>)>>);
            } else {
                static_assert(std::is_same_v<cs, ex::completion_signatures<ex::set_value_t(int)>>);
            }
            const int live_before = state.live_payloads;
            auto run = [&](auto&& source) {
                auto op = ex::connect(std::forward<decltype(source)>(source),
                    observing_receiver{{&state}});
                static_assert(!std::is_move_constructible_v<decltype(op)>);
                const int moves_before = state.moves;
                state.moves_before_throw = 0;
                ex::start(op);
                EXPECT_EQ(state.moves, moves_before);
                expect_success<kind>(state);
            };
            if (copy_lvalue) {
                run(std::as_const(sndr));
                EXPECT_GT(state.copies, 0);
            } else {
                run(std::move(sndr));
                EXPECT_EQ(state.copies, 0);
            }
            expect_inner_first(state);
            EXPECT_EQ(state.live_payloads, live_before);
        }
        EXPECT_EQ(state.live_payloads, 0);
    }
}

TYPED_TEST(ThrowingMoveAdaptors, RealMoveFailuresEscapeConnect) {
    constexpr auto kind = TypeParam::value;
    for (bool copy_lvalue : {false, true}) {
        for (int move_before_throw = 0; move_before_throw < (copy_lvalue ? 1 : 2);
             ++move_before_throw) {
            SCOPED_TRACE(copy_lvalue);
            SCOPED_TRACE(move_before_throw);
            probe state;
            {
                auto sndr = make_sender<kind>(state);
                const int live_before = state.live_payloads;
                state.moves_before_throw = move_before_throw;
                if (copy_lvalue) {
                    EXPECT_THROW((void)ex::connect(std::as_const(sndr),
                        observing_receiver{{&state}}), move_failure);
                } else {
                    EXPECT_THROW((void)ex::connect(std::move(sndr),
                        observing_receiver{{&state}}), move_failure);
                }
                EXPECT_EQ(state.live_payloads, live_before);
                EXPECT_EQ(state.connects, 0);
                EXPECT_EQ(state.starts, 0);
                EXPECT_EQ(state.values + state.errors + state.stopped, 0);
            }
            EXPECT_EQ(state.live_payloads, 0);
        }
    }
}

TYPED_TEST(ThrowingMoveAdaptors, SenderCanDieBeforeStart) {
    constexpr auto kind = TypeParam::value;
    for (bool copy_lvalue : {false, true}) {
        SCOPED_TRACE(copy_lvalue);
        probe state;
        {
            auto make_operation = [&] {
                auto sndr = make_sender<kind>(state);
                if (copy_lvalue) {
                    return ex::connect(std::as_const(sndr), observing_receiver{{&state}});
                }
                return ex::connect(std::move(sndr), observing_receiver{{&state}});
            };
            auto op = make_operation();
            EXPECT_EQ(state.live_payloads, 1);
            EXPECT_TRUE(state.payload_alive);
            state.moves_before_throw = 0;
            ex::start(op);
            expect_success<kind>(state);
        }
        expect_inner_first(state);
        EXPECT_EQ(state.live_payloads, 0);
    }
}

TYPED_TEST(ThrowingMoveAdaptors, UpstreamConnectFailureReleasesOwnedState) {
    constexpr auto kind = TypeParam::value;
    for (bool copy_lvalue : {false, true}) {
        SCOPED_TRACE(copy_lvalue);
        probe state;
        {
            auto sndr = make_sender<kind>(state);
            const int live_before = state.live_payloads;
            state.throw_on_connect = true;
            if (copy_lvalue) {
                EXPECT_THROW((void)ex::connect(std::as_const(sndr),
                    observing_receiver{{&state}}), connect_failure);
            } else {
                EXPECT_THROW((void)ex::connect(std::move(sndr),
                    observing_receiver{{&state}}), connect_failure);
            }
            EXPECT_EQ(state.live_payloads, live_before);
            EXPECT_FALSE(state.payload_alive);
            EXPECT_FALSE(state.outer_alive);
            EXPECT_EQ(state.connects, 1);
            EXPECT_EQ(state.starts, 0);
            EXPECT_EQ(state.inner_destructions, 0);
            EXPECT_EQ(state.values + state.errors + state.stopped, 0);
        }
        EXPECT_EQ(state.live_payloads, 0);
    }
}

TYPED_TEST(ThrowingMoveAdaptors, CompletionFailureNeverMovesStoredState) {
    constexpr auto kind = TypeParam::value;
    probe state;
    {
        auto sndr = make_sender<kind>(state);
        auto op = ex::connect(std::move(sndr), observing_receiver{{&state}});
        const int moves_before = state.moves;
        state.moves_before_throw = 0;
        state.throw_on_call = true;
        ex::start(op);
        EXPECT_EQ(state.moves, moves_before);
        if constexpr (kind == adaptor_kind::stopped_as_error) {
            expect_success<kind>(state);
        } else {
            EXPECT_EQ(state.values, 0);
            EXPECT_EQ(state.errors, 1);
            EXPECT_EQ(state.stopped, 0);
            EXPECT_EQ(state.calls, 1);
            ASSERT_TRUE(state.exception);
            EXPECT_THROW(std::rethrow_exception(state.exception), invocation_failure);
        }
    }
    expect_inner_first(state);
    EXPECT_EQ(state.live_payloads, 0);
}

TYPED_TEST(ThrowingMoveAdaptors, SynchronousCompletionCanDestroyOperation) {
    constexpr auto kind = TypeParam::value;
    for (bool throw_on_call : {false, true}) {
        SCOPED_TRACE(throw_on_call);
        probe state;
        {
            auto sndr = make_sender<kind>(state);
            const int live_before = state.live_payloads;
            using op_t = ex::connect_result_t<decltype(sndr), observing_receiver>;
            auto owner = std::unique_ptr<op_t>(new op_t(ex::connect(
                std::move(sndr), observing_receiver{{&state}})));
            state.destroy = [](void* op) noexcept { delete static_cast<op_t*>(op); };
            state.self_destroy_op = owner.get();
            state.moves_before_throw = 0;
            state.throw_on_call = throw_on_call;
            auto* op = owner.release();
            ex::start(*op);
            EXPECT_EQ(state.self_destroy_op, nullptr);
            EXPECT_EQ(state.starts, 1);
            EXPECT_EQ(state.values + state.errors + state.stopped, 1);
            if (throw_on_call && kind != adaptor_kind::stopped_as_error) {
                ASSERT_TRUE(state.exception);
                EXPECT_THROW(std::rethrow_exception(state.exception), invocation_failure);
            } else {
                expect_success<kind>(state);
            }
            expect_inner_first(state);
            EXPECT_EQ(state.live_payloads, live_before);
        }
        EXPECT_EQ(state.live_payloads, 0);
    }
}

TYPED_TEST(ThrowingMoveAdaptors, UnmatchedCompletionBypassesStoredState) {
    constexpr auto kind = TypeParam::value;
    constexpr bool bypass_with_value = kind == adaptor_kind::upon_error ||
        kind == adaptor_kind::upon_stopped || kind == adaptor_kind::stopped_as_error;
    constexpr auto completion = bypass_with_value ? completion_kind::value : completion_kind::error;
    probe state;
    {
        auto sndr = adapt<kind>(synchronous_sender<completion>{&state}, state);
        auto op = ex::connect(std::move(sndr), observing_receiver{{&state}});
        state.moves_before_throw = 0;
        state.throw_on_call = true;
        ex::start(op);
        EXPECT_EQ(state.calls, 0);
        EXPECT_EQ(state.values, bypass_with_value ? 1 : 0);
        EXPECT_EQ(state.errors, bypass_with_value ? 0 : 1);
        EXPECT_EQ(state.stopped, 0);
        EXPECT_EQ(state.result, 41);
        EXPECT_FALSE(state.exception);
    }
    expect_inner_first(state);
    EXPECT_EQ(state.live_payloads, 0);
}

} // namespace
