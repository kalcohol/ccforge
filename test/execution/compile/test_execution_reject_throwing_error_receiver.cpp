#include <exception>
#include <execution>

struct throwing_error_receiver {
    using receiver_concept = std::execution::receiver_t;

    void set_error(std::exception_ptr) && {}
};

int main() {
    std::execution::set_error(throwing_error_receiver{}, std::exception_ptr{});
}
