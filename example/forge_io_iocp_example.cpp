#include <forge/io.hpp>
#include <execution>
#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

[[nodiscard]] auto byte(char value) noexcept -> std::byte {
    return std::byte{static_cast<unsigned char>(value)};
}

[[noreturn]] void throw_last_error(const char* what) {
    throw std::system_error{
        static_cast<int>(::GetLastError()),
        std::system_category(),
        what};
}

class unique_handle {
public:
    unique_handle() noexcept = default;
    explicit unique_handle(HANDLE handle) noexcept : handle_(handle) {}
    ~unique_handle() noexcept { reset(); }

    unique_handle(unique_handle&& other) noexcept
        : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    auto operator=(unique_handle&& other) noexcept -> unique_handle& {
        if (this != &other) {
            reset(std::exchange(other.handle_, INVALID_HANDLE_VALUE));
        }
        return *this;
    }

    unique_handle(const unique_handle&) = delete;
    auto operator=(const unique_handle&) -> unique_handle& = delete;

    [[nodiscard]] auto get() const noexcept -> HANDLE { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ && handle_ != INVALID_HANDLE_VALUE;
    }

    void reset(HANDLE next = INVALID_HANDLE_VALUE) noexcept {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
        handle_ = next;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

struct pipe_pair {
    unique_handle server;
    unique_handle client;
};

[[nodiscard]] auto make_pipe_pair() -> pipe_pair {
    auto name = std::wstring{LR"(\\.\pipe\ccforge-iocp-example-)"} +
        std::to_wstring(::GetCurrentProcessId()) + L"-" +
        std::to_wstring(::GetTickCount64());

    unique_handle server{::CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,
        4096,
        4096,
        0,
        nullptr)};
    if (!server) {
        throw_last_error("CreateNamedPipeW");
    }

    unique_handle client{::CreateFileW(
        name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr)};
    if (!client) {
        throw_last_error("CreateFileW pipe client");
    }

    if (!::ConnectNamedPipe(server.get(), nullptr) &&
        ::GetLastError() != ERROR_PIPE_CONNECTED) {
        throw_last_error("ConnectNamedPipe");
    }

    return pipe_pair{std::move(server), std::move(client)};
}

} // namespace

int main() {
    auto pipe = make_pipe_pair();
    forge::io::context io;

    std::array<std::byte, 5> payload{
        byte('f'), byte('o'), byte('r'), byte('g'), byte('e')};
    std::array<std::byte, 5> buffer{};

    auto written = std::this_thread::sync_wait(
        io.async_write_some(pipe.client.get(), std::span<const std::byte>{payload}));
    auto read = std::this_thread::sync_wait(
        io.async_read_some(pipe.server.get(), std::span{buffer}));

    if (!written || !read || buffer != payload) {
        return 1;
    }

    std::cout << "IOCP transferred " << std::get<0>(*read) << " bytes\n";
    return 0;
}
