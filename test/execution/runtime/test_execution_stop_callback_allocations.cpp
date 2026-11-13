#include "static_opstate_allocation_probe.hpp"
#include <optional>

#if !defined(FORGE_TEST_STATIC_OPSTATE_TSAN)
namespace {
struct throwing_payload {
    throwing_payload() = default;
    throwing_payload(const throwing_payload&) { throw 42; }
    throwing_payload(throwing_payload&&) { throw 42; }
};

struct allocation_receiver {
    using receiver_concept = std::execution::receiver_t;
    std::inplace_stop_token token;
    int* channel;
    throwing_payload** received;
    void set_value(throwing_payload& value) && noexcept { *channel = 0; *received = &value; }
    void set_error(throwing_payload& value) && noexcept { *channel = 1; *received = &value; }
    void set_stopped() && noexcept { *channel = 2; }
    auto get_env() const noexcept {
        return std::execution::prop(std::execution::get_stop_token, token);
    }
};

struct allocation_sender {
    using sender_concept = std::execution::sender_t;
    throwing_payload* value;
    int channel;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(throwing_payload&),
            std::execution::set_error_t(throwing_payload&),
            std::execution::set_stopped_t()> { return {}; }
    template<class R> struct op {
        using operation_state_concept = std::execution::operation_state_t;
        R receiver;
        throwing_payload* value;
        int channel;
        struct notify {
            op* self;
            void operator()() const noexcept {
                if (self->channel == 0)
                    std::execution::set_value(std::move(self->receiver), *self->value);
                else std::execution::set_error(std::move(self->receiver), *self->value);
            }
        };
        using token_t = std::remove_cvref_t<decltype(std::execution::get_stop_token(
            std::execution::get_env(std::declval<const R&>())))>;
        std::optional<std::stop_callback_for_t<token_t, notify>> registration;
        op(R r, throwing_payload* v, int c) : receiver(std::move(r)), value(v), channel(c) {}
        void start() & noexcept {
            registration.emplace(std::execution::get_stop_token(
                std::execution::get_env(receiver)), notify{this});
        }
    };
    template<class R> auto connect(R r) const -> op<R> {
        return op<R>{std::move(r), value, channel};
    }
};
} // namespace

TEST(StopCallbackAllocationTest, OverlappingCompletionPreservesReferencesWhenAllocationFails) {
    for (int expected : {0, 1}) {
        std::execution::counting_scope scope;
        std::inplace_stop_source source;
        throwing_payload value;
        throwing_payload* received = nullptr;
        int channel = -1;
        auto operation = std::execution::connect(
            scope.get_token().wrap(allocation_sender{&value, expected}),
            allocation_receiver{source.get_token(), &channel, &received});
        std::execution::start(operation);
        // Registration is complete. Reject every allocation in propagation and
        // completion; keep the operation alive until the source call returns.
        const auto allocations = allocation_probe::measure([&] {
            allocation_probe::fail_allocations = true;
            source.request_stop();
            allocation_probe::fail_allocations = false;
        });
        EXPECT_EQ(allocations, 0u);
        EXPECT_EQ(channel, expected);
        EXPECT_EQ(received, &value);
    }
}
#endif
