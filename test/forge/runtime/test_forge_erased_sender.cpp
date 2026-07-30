#include <gtest/gtest.h>
#include <forge/erased_sender.hpp>
#include <forge/static_thread_pool.hpp>
#include "forge_operation_destroy.hpp"
#include <execution>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>

namespace {

using multi_cs = std::execution::completion_signatures<
    std::execution::set_value_t(int),
    std::execution::set_value_t(std::string),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;

struct observed_state {
    int int_values = 0;
    int int_value = 0;
    int string_values = 0;
    std::string string_value;
    int zero_values = 0;
    bool errored = false;
    bool error_code_seen = false;
    std::error_code error_code;
    bool status_seen = false;
    int status_value = 0;
    bool stopped = false;
    bool stop_possible = false;
    bool stop_requested = false;
};

struct observing_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<observed_state> state;

    void set_value(int value) && noexcept {
        ++state->int_values;
        state->int_value = value;
    }

    void set_value(std::string value) && noexcept {
        ++state->string_values;
        state->string_value = std::move(value);
    }

    void set_error(std::exception_ptr error) && noexcept {
        state->errored = static_cast<bool>(error);
    }

    void set_error(std::error_code error) && noexcept {
        state->error_code_seen = true;
        state->error_code = error;
    }

    enum class status {
        failed = 7
    };

    void set_error(status value) && noexcept {
        state->status_seen = true;
        state->status_value = static_cast<int>(value);
    }

    void set_stopped() && noexcept {
        state->stopped = true;
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct reference_value_receiver {
    using receiver_concept = std::execution::receiver_t;

    int* selected_overload = nullptr;
    std::string* observed = nullptr;

    void set_value(const std::string& value) && noexcept {
        *selected_overload = 1;
        *observed = value;
    }

    void set_value(std::string&& value) && noexcept {
        *selected_overload = 2;
        *observed = std::move(value);
    }

    void set_error(std::exception_ptr) && noexcept {
        *selected_overload = -1;
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct reference_error_receiver {
    using receiver_concept = std::execution::receiver_t;

    int* selected_overload = nullptr;
    std::error_code* observed = nullptr;

    void set_error(const std::error_code& error) && noexcept {
        *selected_overload = 1;
        *observed = error;
    }

    void set_error(std::error_code&& error) && noexcept {
        *selected_overload = 2;
        *observed = std::move(error);
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct zero_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<observed_state> state;

    void set_value() && noexcept { ++state->zero_values; }
    void set_error(std::exception_ptr) && noexcept { state->errored = true; }
    void set_stopped() && noexcept { state->stopped = true; }
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

struct self_destroying_zero_receiver {
    using receiver_concept = std::execution::receiver_t;

    forge_test::destroy_context_base* context = nullptr;
    bool* completed = nullptr;

    void set_value() && noexcept {
        *completed = true;
        context->destroy();
    }

    void set_error(std::exception_ptr) && noexcept {
        *completed = true;
        context->destroy();
    }

    void set_stopped() && noexcept {
        *completed = true;
        context->destroy();
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

struct stop_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<observed_state> state;
    std::inplace_stop_source* source;

    void set_value(bool possible, bool requested) && noexcept {
        state->stop_possible = possible;
        state->stop_requested = requested;
    }

    void set_error(std::exception_ptr) && noexcept { state->errored = true; }
    void set_stopped() && noexcept { state->stopped = true; }

    auto get_env() const noexcept {
        return std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_stop_token_t{}, source->get_token()));
    }
};

struct multi_sender {
    using sender_concept = std::execution::sender_t;

    bool use_string = false;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> multi_cs {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        bool use_string;

        op(R r, bool use_str) : rcvr(std::move(r)), use_string(use_str) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;

        void start() & noexcept {
            if (use_string) {
                std::execution::set_value(std::move(rcvr), std::string{"text"});
            } else {
                std::execution::set_value(std::move(rcvr), 42);
            }
        }
    };

    template<class R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{std::move(rcvr), use_string};
    }
};

struct zero_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;
        R rcvr;
        explicit op(R r) : rcvr(std::move(r)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;
        void start() & noexcept { std::execution::set_value(std::move(rcvr)); }
    };

    template<class R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{std::move(rcvr)};
    }
};

struct error_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_error_t(std::exception_ptr)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;
        R rcvr;
        explicit op(R r) : rcvr(std::move(r)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;
        void start() & noexcept {
            std::execution::set_error(
                std::move(rcvr),
                std::make_exception_ptr(std::runtime_error{"boom"}));
        }
    };

    template<class R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{std::move(rcvr)};
    }
};

struct reference_error_sender {
    using sender_concept = std::execution::sender_t;

    std::error_code error = std::make_error_code(std::errc::timed_out);

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_error_t(const std::error_code&)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        const std::error_code* error;

        void start() & noexcept {
            std::execution::set_error(std::move(rcvr), *error);
        }
    };

    template<class R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{std::move(rcvr), &error};
    }
};

struct stopped_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;
        R rcvr;
        explicit op(R r) : rcvr(std::move(r)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;
        void start() & noexcept { std::execution::set_stopped(std::move(rcvr)); }
    };

    template<class R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{std::move(rcvr)};
    }
};

struct stop_probe_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(bool, bool)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;
        R rcvr;
        explicit op(R r) : rcvr(std::move(r)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;
        void start() & noexcept {
            auto token = std::execution::get_stop_token(std::execution::get_env(rcvr));
            std::execution::set_value(
                std::move(rcvr),
                token.stop_possible(),
                token.stop_requested());
        }
    };

    template<class R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{std::move(rcvr)};
    }
};

struct move_only_sender {
    using sender_concept = std::execution::sender_t;

    std::unique_ptr<int> value;

    explicit move_only_sender(int v) : value(std::make_unique<int>(v)) {}
    move_only_sender(move_only_sender&&) noexcept = default;
    move_only_sender& operator=(move_only_sender&&) noexcept = default;
    move_only_sender(const move_only_sender&) = delete;
    move_only_sender& operator=(const move_only_sender&) = delete;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;
        R rcvr;
        int value;
        op(R r, int v) : rcvr(std::move(r)), value(v) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;
        void start() & noexcept {
            std::execution::set_value(std::move(rcvr), std::move(value));
        }
    };

    template<class R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{std::move(rcvr), *value};
    }
};

struct typed_error_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_error_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;
        R rcvr;
        explicit op(R r) : rcvr(std::move(r)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;
        void start() & noexcept { std::execution::set_error(std::move(rcvr), 17); }
    };

    template<class R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{std::move(rcvr)};
    }
};

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
        explicit op(R r) : rcvr(std::move(r)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;
        void start() & noexcept {
            std::execution::set_error(
                std::move(rcvr),
                std::make_error_code(std::errc::timed_out));
        }
    };

    template<class R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{std::move(rcvr)};
    }
};

using status = observing_receiver::status;

struct status_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_error_t(status)> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;
        R rcvr;
        explicit op(R r) : rcvr(std::move(r)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;
        void start() & noexcept {
            std::execution::set_error(std::move(rcvr), status::failed);
        }
    };

    template<class R>
    auto connect(R rcvr) & -> op<R> {
        return op<R>{std::move(rcvr)};
    }
};

using int_cs =
    std::execution::completion_signatures<std::execution::set_value_t(int)>;
using zero_cs =
    std::execution::completion_signatures<std::execution::set_value_t()>;
using error_cs =
    std::execution::completion_signatures<std::execution::set_error_t(std::exception_ptr)>;
using typed_error_cs =
    std::execution::completion_signatures<
        std::execution::set_error_t(std::error_code),
        std::execution::set_error_t(status)>;
using stopped_cs =
    std::execution::completion_signatures<std::execution::set_stopped_t()>;
using stop_probe_cs =
    std::execution::completion_signatures<std::execution::set_value_t(bool, bool)>;

struct reentrant_connect_sender {
    using sender_concept = std::execution::sender_t;

    forge::erased_sender<zero_cs>** self = nullptr;
    bool* reentered = nullptr;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> zero_cs {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;

        explicit op(R r) : rcvr(std::move(r)) {}
        op(op&&) = delete;
        op& operator=(op&&) = delete;
        op(const op&) = delete;
        op& operator=(const op&) = delete;

        void start() & noexcept { std::execution::set_value(std::move(rcvr)); }
    };

    template<class R>
    auto connect(R rcvr) & -> op<R> {
        if (self != nullptr && *self != nullptr && reentered != nullptr && !*reentered) {
            *reentered = true;
            auto nested_state = std::make_shared<observed_state>();
            auto nested = std::execution::connect(**self, zero_receiver{nested_state});
            (void)nested;
        }
        return op<R>{std::move(rcvr)};
    }
};

static_assert(std::execution::sender<forge::erased_sender<int_cs>>);
static_assert(!std::copy_constructible<forge::erased_sender<int_cs>>);
static_assert(std::constructible_from<forge::erased_sender<int_cs>, move_only_sender>);
static_assert(std::constructible_from<forge::erased_sender<
    std::execution::completion_signatures<std::execution::set_error_t(int)>>,
    typed_error_sender>);
static_assert(!std::constructible_from<forge::erased_sender<error_cs>, typed_error_sender>);
static_assert(!std::constructible_from<forge::erased_sender<
    std::execution::completion_signatures<std::execution::set_error_t(status)>>,
    error_code_sender>);

} // namespace

TEST(ErasedSenderTest, DeliversSingleValueShape) {
    forge::erased_sender<multi_cs> sender{multi_sender{}};
    auto state = std::make_shared<observed_state>();
    auto op = std::execution::connect(std::move(sender), observing_receiver{state});

    std::execution::start(op);

    EXPECT_EQ(state->int_values, 1);
    EXPECT_EQ(state->int_value, 42);
    EXPECT_EQ(state->string_values, 0);
}

TEST(ErasedSenderTest, DeliversSelectedValueAlternative) {
    forge::erased_sender<multi_cs> sender{multi_sender{true}};
    auto state = std::make_shared<observed_state>();
    auto op = std::execution::connect(std::move(sender), observing_receiver{state});

    std::execution::start(op);

    EXPECT_EQ(state->int_values, 0);
    EXPECT_EQ(state->string_values, 1);
    EXPECT_EQ(state->string_value, "text");
}

TEST(ErasedSenderTest, PreservesSplitConstReferenceValueCategory) {
    auto split_sender = std::execution::split(
        std::execution::just(std::string{"shared"}));
    using split_sender_t = decltype(split_sender);
    using split_cs_t = std::execution::completion_signatures_of_t<split_sender_t>;
    static_assert(std::constructible_from<
                  forge::erased_sender<split_cs_t>,
                  split_sender_t>);

    forge::erased_sender<split_cs_t> sender{std::move(split_sender)};
    int selected_overload = 0;
    std::string observed;
    auto op = std::execution::connect(
        std::move(sender),
        reference_value_receiver{&selected_overload, &observed});

    std::execution::start(op);

    EXPECT_EQ(selected_overload, 1);
    EXPECT_EQ(observed, "shared");
}

TEST(ErasedSenderTest, DeliversZeroValueShape) {
    forge::erased_sender<zero_cs> sender{zero_sender{}};
    auto state = std::make_shared<observed_state>();
    auto op = std::execution::connect(std::move(sender), zero_receiver{state});

    std::execution::start(op);

    EXPECT_EQ(state->zero_values, 1);
}

TEST(ErasedSenderTest, ConstSenderRemainsConnectable) {
    const forge::erased_sender<zero_cs> sender{zero_sender{}};
    auto state = std::make_shared<observed_state>();
    auto op = std::execution::connect(sender, zero_receiver{state});

    std::execution::start(op);

    EXPECT_EQ(state->zero_values, 1);
}

TEST(ErasedSenderTest, DeliversExceptionPtrError) {
    forge::erased_sender<error_cs> sender{error_sender{}};
    auto state = std::make_shared<observed_state>();
    auto op = std::execution::connect(std::move(sender), observing_receiver{state});

    std::execution::start(op);

    EXPECT_TRUE(state->errored);
}

TEST(ErasedSenderTest, DeliversErrorCodeTypedError) {
    forge::erased_sender<typed_error_cs> sender{error_code_sender{}};
    auto state = std::make_shared<observed_state>();
    auto op = std::execution::connect(std::move(sender), observing_receiver{state});

    std::execution::start(op);

    EXPECT_TRUE(state->error_code_seen);
    EXPECT_EQ(state->error_code, std::make_error_code(std::errc::timed_out));
    EXPECT_FALSE(state->status_seen);
}

TEST(ErasedSenderTest, PreservesTypedErrorReferenceCategory) {
    using cs_t = std::execution::completion_signatures<
        std::execution::set_error_t(const std::error_code&)>;
    forge::erased_sender<cs_t> sender{reference_error_sender{}};
    int selected_overload = 0;
    std::error_code observed;
    auto op = std::execution::connect(
        std::move(sender),
        reference_error_receiver{&selected_overload, &observed});

    std::execution::start(op);

    EXPECT_EQ(selected_overload, 1);
    EXPECT_EQ(observed, std::make_error_code(std::errc::timed_out));
}

TEST(ErasedSenderTest, DeliversEnumTypedError) {
    forge::erased_sender<typed_error_cs> sender{status_sender{}};
    auto state = std::make_shared<observed_state>();
    auto op = std::execution::connect(std::move(sender), observing_receiver{state});

    std::execution::start(op);

    EXPECT_FALSE(state->error_code_seen);
    EXPECT_TRUE(state->status_seen);
    EXPECT_EQ(state->status_value, 7);
}

TEST(ErasedSenderTest, DeliversStopped) {
    forge::erased_sender<stopped_cs> sender{stopped_sender{}};
    auto state = std::make_shared<observed_state>();
    auto op = std::execution::connect(std::move(sender), observing_receiver{state});

    std::execution::start(op);

    EXPECT_TRUE(state->stopped);
}

TEST(ErasedSenderTest, PropagatesReceiverStopToken) {
    forge::erased_sender<stop_probe_cs> sender{stop_probe_sender{}};
    auto state = std::make_shared<observed_state>();
    std::inplace_stop_source source;
    source.request_stop();
    auto op = std::execution::connect(
        std::move(sender),
        stop_receiver{state, &source});

    std::execution::start(op);

    EXPECT_TRUE(state->stop_possible);
    EXPECT_TRUE(state->stop_requested);
}

TEST(ErasedSenderTest, AcceptsMoveOnlySourceSender) {
    forge::erased_sender<int_cs> sender{move_only_sender{7}};
    auto state = std::make_shared<observed_state>();
    auto op = std::execution::connect(std::move(sender), observing_receiver{state});

    std::execution::start(op);

    EXPECT_EQ(state->int_values, 1);
    EXPECT_EQ(state->int_value, 7);
}

TEST(ErasedSenderTest, SourceConnectCanReenterSameErasedSender) {
    forge::erased_sender<zero_cs>* self = nullptr;
    bool reentered = false;
    forge::erased_sender<zero_cs> sender{reentrant_connect_sender{&self, &reentered}};
    self = &sender;
    auto state = std::make_shared<observed_state>();

    auto op = std::execution::connect(sender, zero_receiver{state});
    std::execution::start(op);

    EXPECT_TRUE(reentered);
    EXPECT_EQ(state->zero_values, 1);
}

TEST(ErasedSenderTest, EmptyConnectThrows) {
    forge::erased_sender<zero_cs> sender;
    auto state = std::make_shared<observed_state>();

    EXPECT_THROW(
        (void)std::execution::connect(std::move(sender), zero_receiver{state}),
        std::runtime_error);
}

TEST(ErasedSenderTest, SynchronousSourceAllowsReceiverToDestroyOperation) {
    forge::erased_sender<zero_cs> sender{zero_sender{}};

    using sender_t = decltype(sender);
    using receiver_t = self_destroying_zero_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t&&>(),
        std::declval<receiver_t>()));

    bool completed = false;
    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};

    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            std::move(sender),
            self_destroying_zero_receiver{&context, &completed});
    });
    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);
}

TEST(ErasedSenderTest, DeliversFromSchedulerThread) {
    forge::static_thread_pool pool{1};
    const auto caller_thread = std::this_thread::get_id();
    std::thread::id completion_thread;

    using cs_t = std::execution::completion_signatures<
        std::execution::set_value_t(int),
        std::execution::set_error_t(std::exception_ptr),
        std::execution::set_stopped_t()>;
    forge::erased_sender<cs_t> sender{
        std::execution::starts_on(
            pool.get_scheduler(),
            std::execution::just()
            | std::execution::then([&] noexcept {
                completion_thread = std::this_thread::get_id();
                return 23;
            }))};

    auto result = std::execution::sync_wait(std::move(sender));
    pool.wait();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 23);
    EXPECT_NE(completion_thread, caller_thread);
}
