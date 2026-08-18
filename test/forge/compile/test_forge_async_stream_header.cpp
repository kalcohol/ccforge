#include <forge/io/async_stream.hpp>

#include <cstddef>
#include <type_traits>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
namespace {

class immovable_awaitable {
public:
    immovable_awaitable() = default;
    immovable_awaitable(const immovable_awaitable&) = delete;
    auto operator=(const immovable_awaitable&) -> immovable_awaitable& = delete;
    immovable_awaitable(immovable_awaitable&&) = delete;
    auto operator=(immovable_awaitable&&) -> immovable_awaitable& = delete;

    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return true;
    }

    auto await_suspend(
        std::coroutine_handle<>,
        const forge::io::io_env*) const noexcept -> bool {
        return false;
    }

    [[nodiscard]] auto await_resume()
        -> forge::io::io_result<std::size_t> {
        return forge::io::io_result<std::size_t>::success(0);
    }
};

struct immovable_async_read_stream {
    [[nodiscard]] auto read_some(forge::io::mutable_buffer)
        -> immovable_awaitable {
        return {};
    }
};

class oversized_awaitable {
public:
    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return true;
    }

    auto await_suspend(
        std::coroutine_handle<>,
        const forge::io::io_env*) const noexcept -> bool {
        return false;
    }

    [[nodiscard]] auto await_resume()
        -> forge::io::io_result<std::size_t> {
        return forge::io::io_result<std::size_t>::success(0);
    }

private:
    std::byte storage_[forge::io::erased_io_awaitable_size + 1]{};
};

struct oversized_async_read_stream {
    [[nodiscard]] auto read_some(forge::io::mutable_buffer)
        -> oversized_awaitable {
        return {};
    }
};

} // namespace

static_assert(forge::io::async_read_stream<immovable_async_read_stream>);
static_assert(forge::io::async_read_stream<oversized_async_read_stream>);
static_assert(std::is_constructible_v<
    forge::io::owning_any_async_read_stream,
    immovable_async_read_stream>);
static_assert(!std::is_constructible_v<
    forge::io::owning_any_async_read_stream,
    oversized_async_read_stream>);
#endif

int main() {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    forge::io::owning_any_async_read_stream stream{
        immovable_async_read_stream{}};
    auto operation = stream.read_some(forge::io::mutable_buffer{});
    (void)operation;
#endif
    return 0;
}
