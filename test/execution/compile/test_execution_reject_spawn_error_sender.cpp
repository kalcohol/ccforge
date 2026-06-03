#include <execution>

int main() {
    std::execution::simple_counting_scope scope;
    std::execution::spawn(std::execution::just_error(1), scope.get_token());
}
