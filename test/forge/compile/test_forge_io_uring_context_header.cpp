#include <forge/io/io_uring_context.hpp>

#if !defined(FORGE_HAS_FORGE_IO_URING_BACKEND)
#error "io_uring header probe requires the backend feature macro"
#endif

int main() {
    forge::io::io_uring_context* context = nullptr;
    return context == nullptr ? 0 : 1;
}
