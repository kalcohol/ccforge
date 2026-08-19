#include <forge/io/io_uring_context.hpp>

#include "forge_counting_resource.hpp"

#include <cerrno>
#include <cstdio>
#include <system_error>

namespace {

constexpr int skip_exit_code = 77;

[[nodiscard]] auto runtime_unavailable(const std::error_code& error) noexcept
    -> bool {
    // ENOMEM covers pre-5.11 RLIMIT_MEMLOCK accounting rejecting the ring
    // allocation; the ON lane still fails hard via the REQUIRED define.
    return error.value() == ENOSYS ||
           error.value() == EPERM ||
           error.value() == EACCES ||
           error.value() == EOPNOTSUPP ||
           error.value() == ENOMEM;
}

} // namespace

int main() {
    forge_test::counting_resource memory;
    try {
        {
            forge::io::io_uring_context context{{
                .memory = &memory,
                .entries = 8}};
            context.close();
            context.request_stop();
            context.shutdown();
            context.wait();
        }
    } catch (const std::system_error& error) {
        std::fprintf(
            stderr,
            "io_uring context unavailable: %s (%d)\n",
            error.what(),
            error.code().value());
        if (memory.outstanding() != 0) {
            return 2;
        }
#if defined(FORGE_TEST_IO_URING_RUNTIME_REQUIRED)
        return 1;
#else
        return runtime_unavailable(error.code()) ? skip_exit_code : 1;
#endif
    }

    if (memory.allocations() == 0 || memory.outstanding() != 0) {
        std::fprintf(
            stderr,
            "unexpected PMR counts: allocations=%zu outstanding=%zu\n",
            memory.allocations(),
            memory.outstanding());
        return 3;
    }
    return 0;
}
