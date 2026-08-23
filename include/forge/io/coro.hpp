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

#include <forge/io/detail/coro_lifetime.hpp>
#include <forge/io/env.hpp>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <exception>
#include <execution>
#include <new>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>
#endif

namespace forge::io {

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace __coro_detail {

template<class T, class Receiver>
class io_task_sender_op;

struct request_fused_stop {
    std::inplace_stop_source* source = nullptr;

    auto operator()() const noexcept -> void {
        if (source != nullptr) {
            source->request_stop();
        }
    }
};

template<class Token, bool = std::stoppable_token_for<Token, request_fused_stop>>
struct optional_stop_callback {
    auto install(Token token, std::inplace_stop_source& source) -> void {
        if (token.stop_requested()) {
            source.request_stop();
        }
    }
};

template<class Token>
struct optional_stop_callback<Token, true> {
    using callback_t = std::stop_callback_for_t<Token, request_fused_stop>;

    auto install(Token token, std::inplace_stop_source& source) -> void {
        if (token.stop_requested()) {
            source.request_stop();
            return;
        }
        if (token.stop_possible()) {
            callback.emplace(std::move(token), request_fused_stop{&source});
        }
    }

    std::optional<callback_t> callback;
};

} // namespace __coro_detail

class read_env_awaitable {
public:
    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return false;
    }

    auto await_suspend(std::coroutine_handle<>, const io_env* env) noexcept
        -> bool {
        env_ = env;
        return false;
    }

    [[nodiscard]] auto await_resume() const -> const io_env& {
        if (env_ == nullptr) {
            throw std::logic_error{"forge::io::io_env is not set"};
        }
        return *env_;
    }

private:
    const io_env* env_ = nullptr;
};

[[nodiscard]] inline auto this_io_env() noexcept -> read_env_awaitable {
    return {};
}

class sender_stopped : public std::exception {
public:
    [[nodiscard]] auto what() const noexcept -> const char* override {
        return "forge::io sender stopped";
    }
};

namespace __coro_detail {

struct await_sender_receiver_env {
    const io_env* env = nullptr;

    friend auto tag_invoke(
        std::execution::get_stop_token_t,
        const await_sender_receiver_env& self) noexcept -> std::inplace_stop_token {
        return self.env ? self.env->stop_token : std::inplace_stop_token{};
    }

    friend auto tag_invoke(
        std::execution::get_scheduler_t,
        const await_sender_receiver_env& self) noexcept -> forge::any_scheduler {
        return self.env ? self.env->executor.scheduler() : forge::any_scheduler{};
    }
};

template<class Sender>
class sender_awaitable {
public:
    explicit sender_awaitable(Sender sender)
        : sender_(std::move(sender))
    {}

    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return false;
    }

    template<class Promise>
    auto await_suspend(std::coroutine_handle<Promise> continuation, const io_env* env)
        -> bool {
        env_ = env;
        continuation_ = continuation;
        if constexpr (std::derived_from<Promise, frame_chain_link>) {
            chain_root_ = static_cast<frame_chain_link&>(
                continuation.promise()).root_link;
            bridge_token_ = chain_root_->publish_bridge();
        }
        try {
            ::new (op_storage()) op_t{
                std::execution::connect(std::move(sender_), receiver{this})};
            op_constructed_ = true;
            std::execution::start(*op_ptr());
        } catch (...) {
            clear_published_bridge();
            throw;
        }
        return exchange_bridge_state(bridge_state::suspended) !=
            bridge_state::completed;
    }

    auto await_resume() {
        if (chain_root_ != nullptr) {
            clear_published_bridge();
        }
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        if (stopped_) {
            throw sender_stopped{};
        }
        return std::move(*result_);
    }

    ~sender_awaitable() noexcept {
        // Claim the completion slot before tearing down the operation. An
        // abandoning destruction can lose the backend's done race to an
        // in-flight delivery; that delivery still writes the result
        // members (which stay alive until this destructor finishes) and
        // is waited out by the operation destructor, but it must not
        // resume the coroutine being destroyed. Normal post-completion
        // destruction sees completed here and proceeds unchanged.
        (void)exchange_bridge_state(bridge_state::abandoned);
        destroy_op();
    }

    sender_awaitable(sender_awaitable&& other)
        : sender_(__take_unstarted_sender(other))
    {}
    auto operator=(sender_awaitable&&) -> sender_awaitable& = delete;
    sender_awaitable(const sender_awaitable&) = delete;
    auto operator=(const sender_awaitable&) -> sender_awaitable& = delete;

private:
    struct receiver {
        using receiver_concept = std::execution::receiver_t;

        sender_awaitable* self = nullptr;

        template<class... Vs>
        auto set_value(Vs&&... values) && noexcept -> void {
            try {
                using tuple_t = std::tuple<std::decay_t<Vs>...>;
                self->result_.emplace(value_from_tuple<value_t>(
                    tuple_t{static_cast<Vs&&>(values)...}));
            } catch (...) {
                self->exception_ = std::current_exception();
            }
            self->complete();
        }

        template<class Error>
        auto set_error(Error&& error) && noexcept -> void {
            if constexpr (std::is_same_v<std::decay_t<Error>, std::exception_ptr>) {
                self->exception_ = static_cast<Error&&>(error);
            } else {
                self->exception_ = std::make_exception_ptr(static_cast<Error&&>(error));
            }
            self->complete();
        }

        auto set_stopped() && noexcept -> void {
            self->stopped_ = true;
            self->complete();
        }

        [[nodiscard]] auto get_env() const noexcept -> await_sender_receiver_env {
            return await_sender_receiver_env{self->env_};
        }
    };

    using env_t = await_sender_receiver_env;
    using completion_signatures_t = decltype(std::execution::get_completion_signatures(
        std::declval<Sender>(),
        std::declval<env_t>()));
    using value_t = single_value_or_variant_t<completion_signatures_t>;
    using op_t = std::execution::connect_result_t<Sender, receiver>;

    [[nodiscard]] auto op_storage() noexcept -> void* {
        return static_cast<void*>(op_buffer_);
    }

    [[nodiscard]] auto op_ptr() noexcept -> op_t* {
        return static_cast<op_t*>(op_storage());
    }

    auto destroy_op() noexcept -> void {
        if (op_constructed_) {
            op_ptr()->~op_t();
            op_constructed_ = false;
        }
    }

    [[nodiscard]] auto exchange_bridge_state(bridge_state desired) noexcept
        -> bridge_state {
        if (chain_root_ != nullptr) {
            return chain_root_->transition_bridge(bridge_token_, desired);
        }
        return state_.exchange(desired, std::memory_order_acq_rel);
    }

    auto clear_published_bridge() noexcept -> void {
        if (chain_root_ != nullptr) {
            chain_root_->clear_bridge(bridge_token_);
            chain_root_ = nullptr;
            bridge_token_ = 0;
        }
    }

    static auto __take_unstarted_sender(sender_awaitable& other) -> Sender {
        if (other.op_constructed_ || other.env_ != nullptr ||
            other.continuation_ ||
            other.state_.load(std::memory_order_acquire) !=
                bridge_state::starting) {
            throw std::logic_error{
                "forge::io::await_sender cannot move after suspension starts"};
        }
        return std::move(other.sender_);
    }

    auto complete() noexcept -> void {
        // Resume only when the coroutine is genuinely parked: starting
        // means the synchronous path picks the result up in await_suspend,
        // abandoned means the destructor owns the frame and a resume would
        // race the destruction. Everything the tail needs is copied to
        // locals first: the resume may run the chain to completion and a
        // receiver completing inline may destroy the operation state, so
        // the tail must not touch this awaitable and may touch the root
        // link only while its resuming count keeps the frames alive.
        auto* const root = chain_root_;
        if (exchange_bridge_state(bridge_state::completed) ==
            bridge_state::suspended) {
            if (root == nullptr) {
                continuation_.resume();
                return;
            }
            resume_with_credit(continuation_, root);
        }
    }

    Sender sender_;
    const io_env* env_ = nullptr;
    std::coroutine_handle<> continuation_{};
    std::optional<value_t> result_{};
    std::exception_ptr exception_{};
    bool stopped_ = false;
    alignas(op_t) unsigned char op_buffer_[sizeof(op_t)];
    bool op_constructed_ = false;
    frame_chain_link* chain_root_ = nullptr;
    frame_chain_link::bridge_token bridge_token_ = 0;
    std::atomic<bridge_state> state_{bridge_state::starting};
};

} // namespace __coro_detail

template<std::execution::sender Sender>
[[nodiscard]] auto await_sender(Sender&& sender) {
    return __coro_detail::sender_awaitable<std::decay_t<Sender>>{
        static_cast<Sender&&>(sender)};
}

template<class T>
class io_task {
public:
    struct promise_type : __coro_detail::promise_base<promise_type> {
        __coro_detail::task_result_storage<T> result{};

        [[nodiscard]] auto get_return_object() noexcept -> io_task {
            return io_task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        template<class U>
            requires std::constructible_from<T, U&&>
        auto return_value(U&& value) -> void {
            result.template emplace<1>(static_cast<U&&>(value));
        }

        auto unhandled_exception() noexcept -> void {
            result.template emplace<2>(std::current_exception());
        }
    };

    io_task(io_task&& other) noexcept
        : coro_(std::exchange(other.coro_, {}))
    {}

    auto operator=(io_task&& other) noexcept -> io_task& {
        if (this != &other) {
            __destroy_chain();
            coro_ = std::exchange(other.coro_, {});
        }
        return *this;
    }

    io_task(const io_task&) = delete;
    auto operator=(const io_task&) -> io_task& = delete;

    ~io_task() {
        __destroy_chain();
    }

    [[nodiscard]] auto done() const noexcept -> bool {
        return !coro_ || coro_.done();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(coro_);
    }

    [[nodiscard]] auto result() && -> T {
        if (!coro_) {
            throw std::logic_error{"forge::io::io_task is empty"};
        }
        if (!done()) {
            throw std::logic_error{"forge::io::io_task is not done"};
        }

        auto& storage = coro_.promise().result;
        if (storage.index() == 2) {
            std::rethrow_exception(std::get<2>(storage));
        }
        if (storage.index() != 1) {
            throw std::logic_error{"forge::io::io_task has no result"};
        }
        return std::move(std::get<1>(storage));
    }

private:
    friend class __coro_detail::task_awaitable<T>;
    template<class, class>
    friend class __coro_detail::io_task_sender_op;

    explicit io_task(std::coroutine_handle<promise_type> coro) noexcept
        : coro_(coro)
    {}

    auto __destroy_chain() noexcept -> void {
        if (coro_) {
            __coro_detail::abandon_task_chain(coro_);
            coro_ = {};
        }
    }

    // Forgets the frame without destroying it; destroy_frame_chain owns it.
    auto __abandon() noexcept -> void {
        coro_ = {};
    }

    auto __start_borrowed(const io_env& env) -> void {
        const auto continuation = __borrowed_handle(env);
        auto* const root = coro_
            ? static_cast<__coro_detail::frame_chain_link*>(
                  coro_.promise().root_link)
            : nullptr;
        __coro_detail::resume_with_credit(continuation, root);
    }

    [[nodiscard]] auto __borrowed_handle(const io_env& env) noexcept
        -> std::coroutine_handle<> {
        if (!coro_ || coro_.done()) {
            return std::noop_coroutine();
        }
        coro_.promise().env = &env;
        // self_frame lets a deferred abandonment (receiver completing
        // inline destroyed the operation state) hand the frames to the
        // tail of the unwinding resume for destruction.
        coro_.promise().self_frame = coro_;
        coro_.promise().started.store(true, std::memory_order_release);
        return coro_;
    }

    auto __set_completion(
        void* object,
        void (*complete)(void*) noexcept) noexcept -> void {
        if (coro_) {
            coro_.promise().completion_object = object;
            coro_.promise().complete = complete;
        }
    }

    auto __set_continuation(std::coroutine_handle<> continuation) noexcept
        -> void {
        if (coro_) {
            coro_.promise().continuation = continuation;
        }
    }

    std::coroutine_handle<promise_type> coro_{};
};

template<>
class io_task<void> {
public:
    struct promise_type : __coro_detail::promise_base<promise_type> {
        bool returned = false;
        std::exception_ptr error{};

        [[nodiscard]] auto get_return_object() noexcept -> io_task {
            return io_task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto return_void() noexcept -> void {
            returned = true;
        }

        auto unhandled_exception() noexcept -> void {
            error = std::current_exception();
        }
    };

    io_task(io_task&& other) noexcept
        : coro_(std::exchange(other.coro_, {}))
    {}

    auto operator=(io_task&& other) noexcept -> io_task& {
        if (this != &other) {
            __destroy_chain();
            coro_ = std::exchange(other.coro_, {});
        }
        return *this;
    }

    io_task(const io_task&) = delete;
    auto operator=(const io_task&) -> io_task& = delete;

    ~io_task() {
        __destroy_chain();
    }

    [[nodiscard]] auto done() const noexcept -> bool {
        return !coro_ || coro_.done();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(coro_);
    }

    auto result() && -> void {
        if (!coro_) {
            throw std::logic_error{"forge::io::io_task is empty"};
        }
        if (!done()) {
            throw std::logic_error{"forge::io::io_task is not done"};
        }

        if (coro_.promise().error) {
            std::rethrow_exception(coro_.promise().error);
        }
        if (!coro_.promise().returned) {
            throw std::logic_error{"forge::io::io_task has no result"};
        }
    }

private:
    friend class __coro_detail::task_awaitable<void>;
    template<class, class>
    friend class __coro_detail::io_task_sender_op;

    explicit io_task(std::coroutine_handle<promise_type> coro) noexcept
        : coro_(coro)
    {}

    auto __destroy_chain() noexcept -> void {
        if (coro_) {
            __coro_detail::abandon_task_chain(coro_);
            coro_ = {};
        }
    }

    // Forgets the frame without destroying it; destroy_frame_chain owns it.
    auto __abandon() noexcept -> void {
        coro_ = {};
    }

    auto __start_borrowed(const io_env& env) -> void {
        const auto continuation = __borrowed_handle(env);
        auto* const root = coro_
            ? static_cast<__coro_detail::frame_chain_link*>(
                  coro_.promise().root_link)
            : nullptr;
        __coro_detail::resume_with_credit(continuation, root);
    }

    [[nodiscard]] auto __borrowed_handle(const io_env& env) noexcept
        -> std::coroutine_handle<> {
        if (!coro_ || coro_.done()) {
            return std::noop_coroutine();
        }
        coro_.promise().env = &env;
        // self_frame lets a deferred abandonment (receiver completing
        // inline destroyed the operation state) hand the frames to the
        // tail of the unwinding resume for destruction.
        coro_.promise().self_frame = coro_;
        coro_.promise().started.store(true, std::memory_order_release);
        return coro_;
    }

    auto __set_completion(
        void* object,
        void (*complete)(void*) noexcept) noexcept -> void {
        if (coro_) {
            coro_.promise().completion_object = object;
            coro_.promise().complete = complete;
        }
    }

    auto __set_continuation(std::coroutine_handle<> continuation) noexcept
        -> void {
        if (coro_) {
            coro_.promise().continuation = continuation;
        }
    }

    std::coroutine_handle<promise_type> coro_{};
};

namespace __coro_detail {

template<class T>
class task_awaitable {
public:
    task_awaitable(
        io_task<T> task,
        const io_env** env,
        frame_chain_link* parent) noexcept
        : task_(std::move(task))
        , env_(env)
        , parent_(parent)
    {}

    ~task_awaitable() {
        if (registered_ && parent_ != nullptr) {
            parent_->reset_child();
        }
    }

    task_awaitable(const task_awaitable&) = delete;
    auto operator=(const task_awaitable&) -> task_awaitable& = delete;
    task_awaitable(task_awaitable&&) = delete;
    auto operator=(task_awaitable&&) -> task_awaitable& = delete;

    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return !task_ || task_.done();
    }

    auto await_suspend(std::coroutine_handle<> continuation)
        -> std::coroutine_handle<> {
        if (env_ == nullptr || *env_ == nullptr) {
            missing_env_ = true;
            return continuation;
        }

        task_.__set_continuation(continuation);
        // Record the downward link before control enters the child, so an
        // abandonment while the chain is suspended destroys frames
        // iteratively instead of recursing through this awaitable. The
        // root link propagates so the leaf's sender bridge publishes its
        // completion slot at the root, where abandon_task_chain looks.
        if (parent_ != nullptr && task_.coro_) {
            parent_->child_frame = task_.coro_;
            frame_chain_link* child = &task_.coro_.promise();
            child->root_link = parent_->root_link;
            parent_->child_link = child;
            parent_->owned_task = &task_;
            parent_->release_owned = [](void* task) noexcept {
                static_cast<io_task<T>*>(task)->__abandon();
            };
            registered_ = true;
        }
        const auto child = task_.__borrowed_handle(**env_);
        if (resume_scope::enqueue(parent_->root_link, child)) {
            return std::noop_coroutine();
        }
        return child;
    }

    decltype(auto) await_resume() {
        if (missing_env_) {
            throw std::logic_error{
                "forge::io::io_task continuation has no io_env"};
        }

        if constexpr (std::is_void_v<T>) {
            std::move(task_).result();
        } else {
            return std::move(task_).result();
        }
    }

private:
    io_task<T> task_;
    const io_env** env_ = nullptr;
    frame_chain_link* parent_ = nullptr;
    bool missing_env_ = false;
    bool registered_ = false;
};

template<class T, class Receiver>
class io_task_sender_op {
public:
    using operation_state_concept = std::execution::operation_state_t;
    using receiver_env_t = std::execution::env_of_t<Receiver>;
    using receiver_stop_token_t = decltype(
        std::execution::get_stop_token(std::declval<receiver_env_t>()));

    io_task_sender_op(io_task<T> task, io_env env, Receiver receiver)
        : env_(std::move(env))
        , effective_env_(env_)
        , receiver_(std::move(receiver))
        , task_(std::move(task))
    {}

    io_task_sender_op(io_task_sender_op&&) = delete;
    auto operator=(io_task_sender_op&&) -> io_task_sender_op& = delete;
    io_task_sender_op(const io_task_sender_op&) = delete;
    auto operator=(const io_task_sender_op&) -> io_task_sender_op& = delete;

    auto start() & noexcept -> void {
        try {
            env_stop_.install(env_.stop_token, fused_stop_);
            receiver_stop_.install(
                std::execution::get_stop_token(
                    std::execution::get_env(receiver_)),
                fused_stop_);
            effective_env_.stop_token = fused_stop_.get_token();
            task_.__set_completion(this, &complete_callback);
            task_.__start_borrowed(effective_env_);
        } catch (...) {
            std::execution::set_error(std::move(receiver_), std::current_exception());
        }
    }

private:
    static auto complete_callback(void* self) noexcept -> void {
        static_cast<io_task_sender_op*>(self)->complete();
    }

    auto complete() noexcept -> void {
        try {
            if constexpr (std::is_void_v<T>) {
                std::move(task_).result();
                std::execution::set_value(std::move(receiver_));
            } else {
                auto value = std::move(task_).result();
                std::execution::set_value(std::move(receiver_), std::move(value));
            }
        } catch (const sender_stopped&) {
            std::execution::set_stopped(std::move(receiver_));
        } catch (...) {
            std::execution::set_error(std::move(receiver_), std::current_exception());
        }
    }

    io_env env_;
    io_env effective_env_;
    std::inplace_stop_source fused_stop_{};
    optional_stop_callback<std::inplace_stop_token> env_stop_;
    optional_stop_callback<receiver_stop_token_t> receiver_stop_;
    Receiver receiver_;
    // Destroy the task first: its abandonment arbitration may wait for an
    // in-flight resume that still completes through receiver_.
    io_task<T> task_;
};

template<class T>
class io_task_sender {
public:
    using sender_concept = std::execution::sender_t;

    io_task_sender(io_task<T> task, io_env env)
        : task_(std::move(task))
        , env_(std::move(env))
    {}

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        if constexpr (std::is_void_v<T>) {
            return std::execution::completion_signatures<
                std::execution::set_value_t(),
                std::execution::set_error_t(std::exception_ptr),
                std::execution::set_stopped_t()>{};
        } else {
            return std::execution::completion_signatures<
                std::execution::set_value_t(T),
                std::execution::set_error_t(std::exception_ptr),
                std::execution::set_stopped_t()>{};
        }
    }

    [[nodiscard]] auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver Receiver>
    auto connect(Receiver receiver) && -> io_task_sender_op<T, Receiver> {
        return connect_impl(std::move(receiver));
    }

    template<std::execution::receiver Receiver>
    auto connect(Receiver receiver) & -> io_task_sender_op<T, Receiver> {
        return connect_impl(std::move(receiver));
    }

private:
    template<class Receiver>
    auto connect_impl(Receiver receiver) -> io_task_sender_op<T, Receiver> {
        if (!task_ || !*task_) {
            throw std::logic_error{
                "forge::io::io_task sender is empty"};
        }
        auto task = std::move(*task_);
        task_.reset();
        return io_task_sender_op<T, Receiver>{
            std::move(task),
            std::move(env_),
            std::move(receiver)};
    }

    std::optional<io_task<T>> task_;
    io_env env_;
};

} // namespace __coro_detail

template<class T>
[[nodiscard]] auto as_sender(io_task<T> task, io_env env = {}) {
    return __coro_detail::io_task_sender<T>{std::move(task), std::move(env)};
}

#endif // __cpp_impl_coroutine

} // namespace forge::io
