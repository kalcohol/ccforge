#include <gtest/gtest.h>
#include <forge/io/combinators.hpp>

#include <memory>
#include <optional>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
namespace {
namespace cio = forge::io;

struct heap_owner {
    void* object = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
    ~heap_owner() { reset(); }
    void reset() noexcept {
        auto* ptr = std::exchange(object, nullptr);
        if (ptr) destroy(ptr);
    }
    template<class Factory> auto emplace(Factory factory) -> decltype(factory())& {
        using op_t = decltype(factory());
        auto* ptr = new op_t(factory());
        object = ptr;
        destroy = [](void* address) noexcept { delete static_cast<op_t*>(address); };
        return *ptr;
    }
};

struct destroying_receiver {
    using receiver_concept = std::execution::receiver_t;
    heap_owner* owner;
    int* completions;
    std::inplace_stop_token token;
    bool destroy_in_completion = true;
    void finish() noexcept {
        auto* box = owner;
        ++*completions;
        if (destroy_in_completion) box->reset();
    }
    template<class... Ts> void set_value(Ts&&...) && noexcept { finish(); }
    template<class E> void set_error(E&&) && noexcept { finish(); }
    void set_stopped() && noexcept { finish(); }
    auto get_env() const noexcept {
        return std::execution::prop(std::execution::get_stop_token, token);
    }
};

static_assert(std::is_reference_v<decltype(std::execution::get_stop_token(
    std::execution::get_env(std::declval<const destroying_receiver&>())))>);

struct stop_inline_sender {
    using sender_concept = std::execution::sender_t;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_stopped_t()> { return {}; }
    template<class R> struct op {
        using operation_state_concept = std::execution::operation_state_t;
        R receiver;
        struct notify {
            op* self;
            void operator()() const noexcept {
                auto* current = self;
                current->registration.reset();
                std::execution::set_stopped(std::move(current->receiver));
            }
        };
        using token_t = decltype(std::execution::get_stop_token(
            std::execution::get_env(std::declval<const R&>())));
        std::optional<std::stop_callback_for_t<token_t, notify>> registration;
        explicit op(R r) : receiver(std::move(r)) {}
        void start() & noexcept {
            registration.emplace(std::execution::get_stop_token(
                std::execution::get_env(receiver)), notify{this});
        }
    };
    template<class R> auto connect(R r) const -> op<R> { return op<R>{std::move(r)}; }
};

auto stopped_task() -> cio::io_task<cio::io_result<int>> {
    co_await cio::await_sender(stop_inline_sender{});
    co_return cio::io_result<int>::success(0);
}

template<class Factory> void destroy_on_stop(Factory factory, bool destroy = true) {
    std::inplace_stop_source source;
    heap_owner owner;
    int completions = 0;
    auto& op = owner.emplace([&] {
        return std::execution::connect(factory(),
            destroying_receiver{&owner, &completions, source.get_token(), destroy});
    });
    std::execution::start(op);
    EXPECT_EQ(completions, 0);
    source.request_stop();
    EXPECT_EQ(completions, 1);
    if (destroy) EXPECT_EQ(owner.object, nullptr);
    else EXPECT_NE(owner.object, nullptr);
}
} // namespace

TEST(ForgeStopCallbackLifetimeTest, TaskReceiverStopKeepsFusedSourceAlive) {
    destroy_on_stop([] { return cio::as_sender(stopped_task()); });
}

TEST(ForgeStopCallbackLifetimeTest, TaskEnvironmentStopKeepsFusedSourceAlive) {
    std::inplace_stop_source source;
    heap_owner owner;
    int completions = 0;
    cio::io_env env;
    env.stop_token = source.get_token();
    auto& op = owner.emplace([&] {
        return std::execution::connect(cio::as_sender(stopped_task(), env),
            destroying_receiver{&owner, &completions, {}});
    });
    std::execution::start(op);
    source.request_stop();
    EXPECT_EQ(completions, 1);
    EXPECT_EQ(owner.object, nullptr);
}

TEST(ForgeStopCallbackLifetimeTest, WhenAllReceiverStopKeepsSharedSourceAlive) {
    destroy_on_stop([] { return cio::when_all_results(stopped_task(), stopped_task()); });
}

TEST(ForgeStopCallbackLifetimeTest, WhenAnyReceiverStopKeepsSharedSourceAlive) {
    destroy_on_stop([] { return cio::when_any_results(stopped_task(), stopped_task()); });
}
// Exercise reference-returning stop queries with a retained operation as well.
TEST(ForgeStopCallbackLifetimeTest, ReferenceTokenReceiverStopDeliversBeforeRequestReturns) {
    destroy_on_stop([] { return cio::as_sender(stopped_task()); }, false);
    destroy_on_stop([] { return cio::when_all_results(stopped_task(), stopped_task()); }, false);
    destroy_on_stop([] { return cio::when_any_results(stopped_task(), stopped_task()); }, false);
}
#endif
