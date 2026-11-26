#include <execution>
#include <optional>

using namespace std::execution::parallel_scheduler_replacement;

#if defined(FORGE_PROXY_QUERY_VALID)
using result_type = std::inplace_stop_token;
#elif FORGE_PROXY_QUERY_CASE == 1
using result_type = const std::inplace_stop_token;
#elif FORGE_PROXY_QUERY_CASE == 2
using result_type = std::inplace_stop_token[1];
#elif FORGE_PROXY_QUERY_CASE == 3
using result_type = std::inplace_stop_token&;
#else
#error "Select a receiver proxy Mandate case"
#endif

// An actual instantiation, not just a signature check of the Mandate.
std::optional<result_type> probe(const receiver_proxy& proxy) {
    return proxy.try_query<result_type>(std::get_stop_token);
}

int main() {}
