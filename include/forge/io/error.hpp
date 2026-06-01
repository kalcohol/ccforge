// MIT License
//
// Copyright (c) 2026 CC Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <execution>
#include <cstddef>
#include <exception>
#include <memory>
#include <system_error>
#include <type_traits>
#include <utility>

namespace forge::io {

enum class error_kind {
    unknown,
    system,
    invalid_handle,
    operation_in_progress,
    would_block
};

struct error {
    error_kind kind = error_kind::unknown;
    std::error_code code{};

    friend bool operator==(const error&, const error&) noexcept = default;
};

namespace __typed_detail {

[[nodiscard]] inline auto classify(std::error_code code) noexcept -> error_kind {
    if (code == std::make_error_code(std::errc::bad_file_descriptor)) {
        return error_kind::invalid_handle;
    }
#if defined(ERROR_INVALID_HANDLE)
    if (code.category() == std::system_category()
            && code.value() == ERROR_INVALID_HANDLE) {
        return error_kind::invalid_handle;
    }
#endif
    if (code == std::make_error_code(std::errc::operation_in_progress)) {
        return error_kind::operation_in_progress;
    }
    if (code == std::make_error_code(std::errc::resource_unavailable_try_again)) {
        return error_kind::would_block;
    }
    return error_kind::system;
}

[[nodiscard]] inline auto from_exception(std::exception_ptr ep) noexcept -> error {
    if (!ep) {
        return {};
    }

    try {
        std::rethrow_exception(ep);
    } catch (const std::system_error& e) {
        return error{classify(e.code()), e.code()};
    } catch (...) {
        return {};
    }
}

template<class R>
struct __void_receiver {
    using receiver_concept = std::execution::receiver_t;

    R rcvr;

    void set_value() && noexcept {
        std::execution::set_value(std::move(rcvr));
    }

    void set_error(std::exception_ptr ep) && noexcept {
        std::execution::set_error(std::move(rcvr), from_exception(std::move(ep)));
    }

    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(rcvr));
    }

    auto get_env() const noexcept(noexcept(std::execution::get_env(rcvr)))
        -> decltype(std::execution::get_env(rcvr)) {
        return std::execution::get_env(rcvr);
    }
};

template<class R>
struct __size_receiver {
    using receiver_concept = std::execution::receiver_t;

    R rcvr;

    void set_value(std::size_t value) && noexcept {
        std::execution::set_value(std::move(rcvr), value);
    }

    void set_error(std::exception_ptr ep) && noexcept {
        std::execution::set_error(std::move(rcvr), from_exception(std::move(ep)));
    }

    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(rcvr));
    }

    auto get_env() const noexcept(noexcept(std::execution::get_env(rcvr)))
        -> decltype(std::execution::get_env(rcvr)) {
        return std::execution::get_env(rcvr);
    }
};

template<class Sender, bool Sized>
struct __sender {
    using sender_concept = std::execution::sender_t;
    using completion_signatures = std::conditional_t<
        Sized,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::size_t),
            std::execution::set_error_t(error),
            std::execution::set_stopped_t()>,
        std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_error_t(error),
            std::execution::set_stopped_t()>>;

    Sender sender;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    struct __op {
        using receiver_t =
            std::conditional_t<Sized, __size_receiver<R>, __void_receiver<R>>;
        using op_t = std::execution::connect_result_t<Sender, receiver_t>;
        struct state_t {
            template<class Factory>
            explicit state_t(Factory&& factory)
                : op(static_cast<Factory&&>(factory)()) {}

            op_t op;
        };

        __op(Sender sender, R rcvr)
            : state(std::make_shared<state_t>([&]() -> op_t {
                  return std::execution::connect(
                      std::move(sender),
                      receiver_t{std::move(rcvr)});
              }))
        {}

        __op(__op&&) = delete;
        auto operator=(__op&&) -> __op& = delete;
        __op(const __op&) = delete;
        auto operator=(const __op&) -> __op& = delete;

        void start() & noexcept {
            auto keepalive = state;
            std::execution::start(keepalive->op);
        }

        std::shared_ptr<state_t> state;
    };

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{std::move(sender), std::move(rcvr)};
    }

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
              && std::copy_constructible<Sender>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{Sender(sender), std::move(rcvr)};
    }
};

template<class Sender>
[[nodiscard]] auto void_sender(Sender&& sender)
    -> __sender<std::decay_t<Sender>, false> {
    return __sender<std::decay_t<Sender>, false>{static_cast<Sender&&>(sender)};
}

template<class Sender>
[[nodiscard]] auto size_sender(Sender&& sender)
    -> __sender<std::decay_t<Sender>, true> {
    return __sender<std::decay_t<Sender>, true>{static_cast<Sender&&>(sender)};
}

} // namespace __typed_detail

} // namespace forge::io
