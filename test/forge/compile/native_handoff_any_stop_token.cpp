#include <forge/any_stop_token.hpp>
#include <stop_token>

int main() {
    std::inplace_stop_source source;
    forge::any_stop_token stop_token{source.get_token()};
    return stop_token.stop_possible() ? 0 : 1;
}
