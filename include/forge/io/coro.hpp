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

#include <forge/any_scheduler.hpp>
#include <forge/resource_policy.hpp>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <exception>
#include <execution>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <variant>
#include <tuple>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>
#endif

namespace forge::io {

class executor_ref {
public:
    executor_ref() = default;

    template<class Scheduler>
        requires (!std::is_same_v<std::remove_cvref_t<Scheduler>, executor_ref>)
              && (!std::is_same_v<std::remove_cvref_t<Scheduler>, forge::any_scheduler>)
              && std::execution::scheduler<std::remove_cvref_t<Scheduler>>
    executor_ref(Scheduler&& scheduler)
        : scheduler_(static_cast<Scheduler&&>(scheduler))
    {}

    executor_ref(forge::any_scheduler scheduler) noexcept
        : scheduler_(std::move(scheduler))
    {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(scheduler_);
    }

    [[nodiscard]] auto schedule() const noexcept {
        return scheduler_.schedule();
    }

    [[nodiscard]] auto scheduler() const noexcept -> const forge::any_scheduler& {
        return scheduler_;
    }

private:
    forge::any_scheduler scheduler_{};
};

struct io_env {
    executor_ref executor{};
    std::inplace_stop_token stop_token{};
    std::pmr::memory_resource* memory = forge::default_memory_resource();
};

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

template<class T>
class io_task;

namespace __coro_detail {

template<class T>
class task_awaitable;

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

template<class... Ts>
struct type_list {};

template<class List, class T>
struct list_contains;

template<class T>
struct list_contains<type_list<>, T> : std::false_type {};

template<class Head, class... Tail, class T>
struct list_contains<type_list<Head, Tail...>, T>
    : std::conditional_t<
          std::is_same_v<Head, T>,
          std::true_type,
          list_contains<type_list<Tail...>, T>> {};

template<class List, class T>
inline constexpr bool list_contains_v = list_contains<List, T>::value;

template<class List, class T>
struct list_push_unique;

template<class... Ts, class T>
struct list_push_unique<type_list<Ts...>, T> {
    using type = std::conditional_t<
        list_contains_v<type_list<Ts...>, T>,
        type_list<Ts...>,
        type_list<Ts..., T>>;
};

template<class List, class T>
using list_push_unique_t = typename list_push_unique<List, T>::type;

template<class List, class Sig>
struct push_value_tuple {
    using type = List;
};

template<class... Tuples, class... Vs>
struct push_value_tuple<type_list<Tuples...>, std::execution::set_value_t(Vs...)> {
    using tuple_t = std::tuple<std::decay_t<Vs>...>;
    using type = list_push_unique_t<type_list<Tuples...>, tuple_t>;
};

template<class List, class... Sigs>
struct collect_value_tuples;

template<class List>
struct collect_value_tuples<List> {
    using type = List;
};

template<class List, class Sig, class... Rest>
struct collect_value_tuples<List, Sig, Rest...> {
    using next = typename push_value_tuple<List, Sig>::type;
    using type = typename collect_value_tuples<next, Rest...>::type;
};

template<class CS>
struct value_tuple_list;

template<class... Sigs>
struct value_tuple_list<std::execution::completion_signatures<Sigs...>> {
    using type = typename collect_value_tuples<type_list<>, Sigs...>::type;
};

template<class CS>
using value_tuple_list_t = typename value_tuple_list<CS>::type;

template<class List>
struct single_value_or_variant_from_list;

template<>
struct single_value_or_variant_from_list<type_list<>> {
    using type = std::tuple<>;
};

template<class Tuple>
struct single_value_or_variant_from_list<type_list<Tuple>> {
    using type = Tuple;
};

template<class... Tuples>
struct single_value_or_variant_from_list<type_list<Tuples...>> {
    using type = std::variant<Tuples...>;
};

template<class List>
using single_value_or_variant_from_list_t =
    typename single_value_or_variant_from_list<List>::type;

template<class CS>
using single_value_or_variant_t =
    single_value_or_variant_from_list_t<value_tuple_list_t<CS>>;

template<class Value, class Tuple>
[[nodiscard]] auto value_from_tuple(Tuple&& tuple) -> Value {
    using tuple_t = std::remove_cvref_t<Tuple>;
    if constexpr (std::is_same_v<Value, tuple_t>) {
        return static_cast<Tuple&&>(tuple);
    } else {
        return Value{std::in_place_type<tuple_t>, static_cast<Tuple&&>(tuple)};
    }
}

template<class T>
concept await_suspend_result =
    std::same_as<T, void> ||
    std::same_as<T, bool> ||
    std::convertible_to<T, std::coroutine_handle<>>;

} // namespace __coro_detail

template<class Awaitable>
concept io_awaitable =
    requires(Awaitable& awaitable,
             std::coroutine_handle<> continuation,
             const io_env* env) {
        { awaitable.await_ready() } -> std::convertible_to<bool>;
        { awaitable.await_suspend(continuation, env) }
            -> __coro_detail::await_suspend_result;
        awaitable.await_resume();
    };

namespace __coro_detail {

template<class Awaitable>
class env_await_adapter {
public:
    explicit env_await_adapter(Awaitable awaitable, const io_env** env) noexcept(
        std::is_nothrow_constructible_v<Awaitable, Awaitable&&>)
        : awaitable_(static_cast<Awaitable&&>(awaitable))
        , env_(env)
    {}

    [[nodiscard]] auto await_ready() noexcept(noexcept(awaitable_.await_ready()))
        -> bool {
        return awaitable_.await_ready();
    }

    template<class Promise>
    auto await_suspend(std::coroutine_handle<Promise> continuation)
        noexcept(noexcept(awaitable_.await_suspend(continuation, *env_))) {
        return awaitable_.await_suspend(continuation, *env_);
    }

    auto await_resume() noexcept(noexcept(awaitable_.await_resume()))
        -> decltype(auto) {
        return awaitable_.await_resume();
    }

private:
    Awaitable awaitable_;
    const io_env** env_;
};

// Coroutine frames allocated through promise_base reserve a pointer-sized
// trailer behind the frame bytes. The trailer records which
// std::pmr::memory_resource owns the frame (null means global operator new),
// so the sized operator delete can route the deallocation without any side
// table. The trailer is written and read with memcpy because the compiler
// only guarantees raw storage there.
inline constexpr std::size_t frame_allocation_alignment =
    __STDCPP_DEFAULT_NEW_ALIGNMENT__;

[[nodiscard]] constexpr auto frame_trailer_offset(std::size_t size) noexcept
    -> std::size_t {
    constexpr std::size_t align = alignof(std::pmr::memory_resource*);
    return ((size + align - 1U) / align) * align;
}

[[nodiscard]] constexpr auto frame_allocation_size(std::size_t size) noexcept
    -> std::size_t {
    return frame_trailer_offset(size) + sizeof(std::pmr::memory_resource*);
}

inline auto stash_frame_resource(
    void* frame,
    std::size_t size,
    std::pmr::memory_resource* memory) noexcept -> void {
    std::memcpy(
        static_cast<unsigned char*>(frame) + frame_trailer_offset(size),
        &memory,
        sizeof(memory));
}

[[nodiscard]] inline auto stashed_frame_resource(
    void* frame,
    std::size_t size) noexcept -> std::pmr::memory_resource* {
    std::pmr::memory_resource* memory = nullptr;
    std::memcpy(
        &memory,
        static_cast<unsigned char*>(frame) + frame_trailer_offset(size),
        sizeof(memory));
    return memory;
}

// Downward links for iterative abandonment. Destroying a suspended
// co_await io_task chain through nested task_awaitable destructors costs
// one set of native stack frames per level and overflows for deep chains,
// so every frame records its awaited child and how to detach the owning
// awaitable; destroy_frame_chain then walks the chain in a loop.
struct frame_chain_link {
    std::coroutine_handle<> child_frame{};
    frame_chain_link* child_link = nullptr;
    void* owned_task = nullptr;
    void (*release_owned)(void*) noexcept = nullptr;

    auto reset_child() noexcept -> void {
        child_frame = {};
        child_link = nullptr;
        owned_task = nullptr;
        release_owned = nullptr;
    }
};

// Destroys a suspended frame chain iteratively from the outermost frame.
// Before each frame dies, the task_awaitable inside it is detached from
// the child frame via release_owned, so the awaitable destructor that
// runs during frame.destroy() does not recurse into the child; the child
// is destroyed by the next loop iteration instead.
inline auto destroy_frame_chain(
    std::coroutine_handle<> frame,
    frame_chain_link* link) noexcept -> void {
    while (frame) {
        std::coroutine_handle<> child{};
        frame_chain_link* child_link = nullptr;
        if (link != nullptr && link->child_frame) {
            child = link->child_frame;
            child_link = link->child_link;
            link->release_owned(link->owned_task);
        }
        frame.destroy();
        frame = child;
        link = child_link;
    }
}

template<class Promise>
struct promise_base : frame_chain_link {
    const io_env* env = nullptr;
    std::coroutine_handle<> continuation{};
    void* completion_object = nullptr;
    void (*complete)(void*) noexcept = nullptr;

    // P4127 explicit-parameter frame allocation: a coroutine whose parameter
    // list starts with (std::allocator_arg_t, std::pmr::memory_resource*)
    // allocates its frame from that resource; every other coroutine keeps
    // the global operator new path. Allocation failure propagates as an
    // exception; get_return_object_on_allocation_failure is intentionally
    // not provided so a throwing resource cannot be mistaken for an empty
    // task.
    static auto operator new(std::size_t size) -> void* {
        void* frame = ::operator new(frame_allocation_size(size));
        stash_frame_resource(frame, size, nullptr);
        return frame;
    }

    template<class... Args>
    static auto operator new(
        std::size_t size,
        std::allocator_arg_t,
        std::pmr::memory_resource* memory,
        const Args&...) -> void* {
        if (memory == nullptr) {
            return operator new(size);
        }
        void* frame = memory->allocate(
            frame_allocation_size(size),
            frame_allocation_alignment);
        stash_frame_resource(frame, size, memory);
        return frame;
    }

    // [dcl.fct.def.coroutine]/9 passes the implicit object parameter of a
    // member or lambda coroutine ahead of the declared parameters (GCC
    // does; current Clang does not), so without this overload the same
    // lambda coroutine silently fell back to the global heap on one
    // compiler and used the resource on the other. Mirroring
    // std::generator's allocator protocol, any first argument in front of
    // the (allocator_arg, resource) pair is treated as the object
    // parameter.
    template<class This, class... Args>
    static auto operator new(
        std::size_t size,
        const This&,
        std::allocator_arg_t,
        std::pmr::memory_resource* memory,
        const Args&...) -> void* {
        return operator new(size, std::allocator_arg, memory);
    }

    static auto operator delete(void* frame, std::size_t size) noexcept
        -> void {
        std::pmr::memory_resource* memory = stashed_frame_resource(frame, size);
        if (memory == nullptr) {
            ::operator delete(frame, frame_allocation_size(size));
            return;
        }
        memory->deallocate(
            frame,
            frame_allocation_size(size),
            frame_allocation_alignment);
    }

    [[nodiscard]] auto initial_suspend() noexcept -> std::suspend_always {
        return {};
    }

    struct final_awaiter {
        [[nodiscard]] auto await_ready() noexcept -> bool {
            return false;
        }

        template<class P>
        auto await_suspend(std::coroutine_handle<P> continuation) noexcept
            -> std::coroutine_handle<> {
            auto& promise = continuation.promise();
            if (promise.complete != nullptr) {
                promise.complete(promise.completion_object);
                return std::noop_coroutine();
            }
            if (promise.continuation) {
                return promise.continuation;
            }
            return std::noop_coroutine();
        }

        auto await_resume() noexcept -> void {}
    };

    [[nodiscard]] auto final_suspend() noexcept -> final_awaiter {
        return {};
    }

    template<class T>
    [[nodiscard]] auto await_transform(io_task<T>&& task) {
        return task_awaitable<T>{std::move(task), &env, this};
    }

    template<class Awaitable>
        requires io_awaitable<Awaitable>
    [[nodiscard]] auto await_transform(Awaitable&& awaitable) {
        using awaitable_t = std::conditional_t<
            std::is_lvalue_reference_v<Awaitable>,
            Awaitable,
            std::remove_cvref_t<Awaitable>>;
        return env_await_adapter<awaitable_t>{
            static_cast<Awaitable&&>(awaitable),
            &env};
    }

    template<class Awaitable>
        requires (!io_awaitable<Awaitable>)
    [[nodiscard]] decltype(auto) await_transform(Awaitable&& awaitable) {
        if constexpr (std::is_lvalue_reference_v<Awaitable>) {
            return static_cast<Awaitable&&>(awaitable);
        } else {
            using awaitable_t = std::remove_cvref_t<Awaitable>;
            return awaitable_t{static_cast<Awaitable&&>(awaitable)};
        }
    }
};

template<class T>
using task_result_storage = std::variant<std::monostate, T, std::exception_ptr>;

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
        ::new (op_storage()) op_t{
            std::execution::connect(std::move(sender_), receiver{this})};
        op_constructed_ = true;
        std::execution::start(*op_ptr());
        return state_.exchange(
            completion_state::suspended,
            std::memory_order_acq_rel) != completion_state::completed;
    }

    auto await_resume() {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        if (stopped_) {
            throw sender_stopped{};
        }
        return std::move(*result_);
    }

    ~sender_awaitable() noexcept {
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

    enum class completion_state {
        starting,
        suspended,
        completed
    };

    static auto __take_unstarted_sender(sender_awaitable& other) -> Sender {
        if (other.op_constructed_ || other.env_ != nullptr ||
            other.continuation_ ||
            other.state_.load(std::memory_order_acquire) !=
                completion_state::starting) {
            throw std::logic_error{
                "forge::io::await_sender cannot move after suspension starts"};
        }
        return std::move(other.sender_);
    }

    auto complete() noexcept -> void {
        if (state_.exchange(
                completion_state::completed,
                std::memory_order_acq_rel) == completion_state::suspended) {
            continuation_.resume();
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
    std::atomic<completion_state> state_{completion_state::starting};
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
            __coro_detail::destroy_frame_chain(coro_, &coro_.promise());
            coro_ = {};
        }
    }

    // Forgets the frame without destroying it; destroy_frame_chain owns it.
    auto __abandon() noexcept -> void {
        coro_ = {};
    }

    auto __start_borrowed(const io_env& env) -> void {
        __borrowed_handle(env).resume();
    }

    [[nodiscard]] auto __borrowed_handle(const io_env& env) noexcept
        -> std::coroutine_handle<> {
        if (!coro_ || coro_.done()) {
            return std::noop_coroutine();
        }
        coro_.promise().env = &env;
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
            __coro_detail::destroy_frame_chain(coro_, &coro_.promise());
            coro_ = {};
        }
    }

    // Forgets the frame without destroying it; destroy_frame_chain owns it.
    auto __abandon() noexcept -> void {
        coro_ = {};
    }

    auto __start_borrowed(const io_env& env) -> void {
        __borrowed_handle(env).resume();
    }

    [[nodiscard]] auto __borrowed_handle(const io_env& env) noexcept
        -> std::coroutine_handle<> {
        if (!coro_ || coro_.done()) {
            return std::noop_coroutine();
        }
        coro_.promise().env = &env;
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
        // iteratively instead of recursing through this awaitable.
        if (parent_ != nullptr && task_.coro_) {
            parent_->child_frame = task_.coro_;
            parent_->child_link = &task_.coro_.promise();
            parent_->owned_task = &task_;
            parent_->release_owned = [](void* task) noexcept {
                static_cast<io_task<T>*>(task)->__abandon();
            };
            registered_ = true;
        }
        return task_.__borrowed_handle(**env_);
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
        , task_(std::move(task))
        , receiver_(std::move(receiver))
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
    io_task<T> task_;
    Receiver receiver_;
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
