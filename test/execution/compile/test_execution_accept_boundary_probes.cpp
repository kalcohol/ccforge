#include <execution>
#include <exception>
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

    std::execution::simple_counting_scope scope;
    std::execution::spawn(std::execution::just(), scope.get_token());
    return 0;
}
