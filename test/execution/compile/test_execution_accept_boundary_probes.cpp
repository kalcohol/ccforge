#include <execution>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

struct noexcept_receiver {
    using receiver_concept = std::execution::receiver_t;

    void set_value() && noexcept {}
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

struct value_receiver {
    using receiver_concept = std::execution::receiver_t;

    void set_value(int) && noexcept {}
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

int main() {
    std::execution::set_value(noexcept_receiver{});
    std::execution::set_error(noexcept_receiver{}, std::exception_ptr{});
    std::execution::set_stopped(noexcept_receiver{});

    auto sender = std::execution::just(42);
    auto op = std::execution::connect(std::move(sender), value_receiver{});
    static_assert(!std::move_constructible<decltype(op)>);
    std::execution::start(op);

    auto move_only = std::execution::just(std::make_unique<int>(7));
    auto adapted = std::execution::then(
        std::move(move_only),
        [](std::unique_ptr<int>) noexcept {});
    auto adapted_op = std::execution::connect(std::move(adapted), noexcept_receiver{});
    std::execution::start(adapted_op);

    std::execution::simple_counting_scope scope;
    std::execution::spawn(std::execution::just(), scope.get_token());

    auto joined = std::execution::when_all(std::execution::just());
    auto joined_op = std::execution::connect(std::move(joined), noexcept_receiver{});
    std::execution::start(joined_op);

    auto continued = std::execution::continues_on(
        std::execution::just(7), std::execution::inline_scheduler{});
    auto continued_op = std::execution::connect(
        std::move(continued), value_receiver{});
    std::execution::start(continued_op);

    auto recovered = std::execution::upon_error(
        std::execution::just_error(5),
        [](int error) noexcept { return error + 1; });
    auto recovered_op = std::execution::connect(
        std::move(recovered), value_receiver{});
    std::execution::start(recovered_op);
    return 0;
}
