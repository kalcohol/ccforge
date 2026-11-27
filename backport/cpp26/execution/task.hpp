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

#include "affine.hpp"
#include "awaitable.hpp"
#include "concepts.hpp"
#include "env.hpp"
#include "inline_scheduler.hpp"
#include "stop_token.hpp"
#include "task_scheduler.hpp"

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#if __has_include(<version>)
#include <version>
#endif

#include "detail/task_frame_allocation.hpp"
#include "detail/task_terminal.hpp"

// std::execution::task: frame ownership and terminal delivery ([task.class],
// [task.members], [task.state], [task.promise]).
//
// Frame allocation ([task.promise]/13 to /19) is in
// detail/task_frame_allocation.hpp.
//
// The operation's environments, the start scheduler selection and the
// promise environment of [task.promise]/12 are here. The stop token of
// [task.state]/7 is not answered yet.
//
// Sender awaits and the class surface are present but not yet qualified.
// No feature macro is defined.

namespace std::execution {

template<class E>
struct with_error {
    using type = std::remove_cvref_t<E>;
    type error;
};

template<class E>
with_error(E) -> with_error<E>;

template<class T = void, class Environment = env<>>
class task;

namespace __forge_task {

template<class Env>
concept __names_allocator_type = requires { typename Env::allocator_type; };

template<class Env>
concept __names_start_scheduler_type = requires { typename Env::start_scheduler_type; };

template<class Env>
concept __names_stop_source_type = requires { typename Env::stop_source_type; };

template<class Env>
concept __names_error_types = requires { typename Env::error_types; };

template<class Env>
struct __allocator_of {
    using type = std::allocator<std::byte>;
};

template<__names_allocator_type Env>
struct __allocator_of<Env> {
    using type = typename Env::allocator_type;
};

template<class Env>
struct __start_scheduler_of {
    using type = task_scheduler;
};

template<__names_start_scheduler_type Env>
struct __start_scheduler_of<Env> {
    using type = typename Env::start_scheduler_type;
};

template<class Env>
struct __stop_source_of {
    using type = std::inplace_stop_source;
};

template<__names_stop_source_type Env>
struct __stop_source_of<Env> {
    using type = typename Env::stop_source_type;
};

template<class Env>
struct __error_types_of {
    using type = completion_signatures<set_error_t(std::exception_ptr)>;
};

template<__names_error_types Env>
struct __error_types_of<Env> {
    using type = typename Env::error_types;
};

// [task.promise]/4: the unique listed error type reachable from the payload.
template<class From, class... Errors>
struct __first_convertible {};

template<class From, class First, class... Rest>
struct __first_convertible<From, First, Rest...>
    : std::conditional_t<std::is_convertible_v<From, First>,
                         std::type_identity<First>, __first_convertible<From, Rest...>> {};

// [task.class]/4: a completion_signatures of set_error_t(E) elements only.
template<class Signature>
struct __error_argument {
    static constexpr bool __is_error = false;
    using type = void;
};

template<class Error>
struct __error_argument<set_error_t(Error)> {
    static constexpr bool __is_error = true;
    using type = Error;
};

template<class Promise, class T, class... Errors>
struct __error_traits {
    using __core = __terminal_core<Promise, T, Errors...>;

    static constexpr bool __has_exception_ptr = __lists_exception_ptr<Errors...>;

    template<class From>
    static constexpr bool __reachable = (std::is_convertible_v<From, Errors> || ...);

    template<class From>
    using __target = typename __first_convertible<From, Errors...>::type;

    // A repeated list element still names one type.
    template<class From>
    static constexpr bool __unique_target =
        ((!std::is_convertible_v<From, Errors> || std::same_as<Errors, __target<From>>) && ...);

    template<class Rcvr>
    static void __send(Rcvr& __rcvr, std::size_t __index, void* __referent) noexcept {
        __forge_task::__send_error<Rcvr, Errors...>(__rcvr, __index, __referent);
    }

    template<class Rcvr>
    static void __send(Rcvr& __rcvr, __outcome<T>& __data) noexcept {
        __forge_task::__send_result<Rcvr, T, Errors...>(__rcvr, __data);
    }
};

template<class ErrorTypes>
struct __error_list {
    static constexpr bool __valid_list = false;
};

template<class... Signatures>
struct __error_list<completion_signatures<Signatures...>> {
    static constexpr bool __valid_list = (__error_argument<Signatures>::__is_error && ...);

    template<class Promise, class T>
    using __traits = __error_traits<Promise, T, typename __error_argument<Signatures>::type...>;

    template<class... Value>
    using __signatures = completion_signatures<Value..., Signatures..., set_stopped_t()>;
};

// [optional.ref.assign] constrains optional<U&>::emplace with
// reference_constructs_from_temporary. libc++ 19 has the compiler builtin
// but not the library trait.
#if defined(__cpp_lib_reference_from_temporary) && __cpp_lib_reference_from_temporary >= 202202L
#define FORGE_TASK_REFERENCE_TRAIT 1
#elif defined(__has_builtin)
#if __has_builtin(__reference_constructs_from_temporary)
#define FORGE_TASK_REFERENCE_TRAIT 2
#endif
#endif

// __reference_binding_branch records the choice: 1 is the library trait and
// 2 the compiler builtin, which are both exact. 0 is the approximation.
#if !defined(FORGE_TASK_REFERENCE_TRAIT)
// Without compiler support, accept only a direct binding to an lvalue of a
// reference-related type. This rejects some valid conversion-function cases.
inline constexpr int __reference_binding_branch = 0;
template<class Ref, class From>
inline constexpr bool __binds_temporary =
    !(std::is_lvalue_reference_v<From> &&
      std::is_convertible_v<std::remove_reference_t<From>*, std::remove_reference_t<Ref>*>);
#elif FORGE_TASK_REFERENCE_TRAIT == 1
inline constexpr int __reference_binding_branch = 1;
template<class Ref, class From>
inline constexpr bool __binds_temporary = std::reference_constructs_from_temporary_v<Ref, From>;
#else
inline constexpr int __reference_binding_branch = 2;
template<class Ref, class From>
inline constexpr bool __binds_temporary = __reference_constructs_from_temporary(Ref, From);
#endif
#undef FORGE_TASK_REFERENCE_TRAIT

template<class T, class V>
inline constexpr bool __returnable = true;

// The lvalue case follows optional<U&>::emplace. The draft admits an rvalue
// reference result but has no optional<U&&> to constrain it.
template<class U, class V>
inline constexpr bool __returnable<U&, V> =
    std::is_constructible_v<U&, V> && !__binds_temporary<U&, V>;

template<class U, class V>
inline constexpr bool __returnable<U&&, V> = std::is_constructible_v<U&&, V>;

// [task.promise]/10 and /11: exactly one of the two return hooks exists.
template<class T, class Core>
struct __promise_return {
    Core* __core_ = nullptr;

    template<class V = T>
        requires __returnable<T, V>
    void return_value(V&& __operand) {
        static_assert(std::is_constructible_v<T, V>,
                      "task<T>: the co_return operand must construct T");
        __core_->__outcome_->__result.__emplace(static_cast<V&&>(__operand));
    }
};

template<class Core>
struct __promise_return<void, Core> {
    Core* __core_ = nullptr;

    void return_void() noexcept {}
};

template<class Env, class ReceiverEnv>
concept __names_env_type = requires { typename Env::template env_type<ReceiverEnv>; };

template<class Env, class ReceiverEnv>
struct __own_env_of {
    using type = env<>;
};

template<class Env, class ReceiverEnv>
    requires __names_env_type<Env, ReceiverEnv>
struct __own_env_of<Env, ReceiverEnv> {
    using type = typename Env::template env_type<ReceiverEnv>;
};

template<class Own, class Rcvr>
concept __own_env_from_receiver = requires(Rcvr& __rcvr) { Own(std::execution::get_env(__rcvr)); };

// The draft's fallback expression T(), which a const type also satisfies.
template<class T>
concept __value_initializable = requires { T(); };

// [task.state]/2, third item.
template<class Own, class Rcvr>
Own __make_own_env(Rcvr& __rcvr) {
    if constexpr (__own_env_from_receiver<Own, Rcvr>) {
        return Own(std::execution::get_env(__rcvr));
    } else {
        static_assert(__value_initializable<Own>,
                      "task: own-env-t needs get_env(rcvr) or default construction");
        return Own();
    }
}

template<class Env, class Own>
concept __environment_from_own = requires(Own& __own) { Env(__own); };

template<class Env, class Rcvr>
concept __environment_from_receiver = requires(Rcvr& __rcvr) { Env(std::execution::get_env(__rcvr)); };

// [task.state]/2, fourth item.
template<class Env, class Own, class Rcvr>
Env __make_environment(Own& __own, Rcvr& __rcvr) {
    if constexpr (__environment_from_own<Env, Own>) {
        return Env(__own);
    } else if constexpr (__environment_from_receiver<Env, Rcvr>) {
        return Env(std::execution::get_env(__rcvr));
    } else {
        static_assert(__value_initializable<Env>,
                      "task: Environment needs own-env, get_env(rcvr) or default construction");
        return Env();
    }
}

// [task.state]/4. ev is the object get_env(rcvr), named as an lvalue.
template<class Ev>
concept __has_start_scheduler = requires(Ev& __ev) { std::execution::get_start_scheduler(__ev); };

template<class Ev>
concept __has_scheduler_allocator = requires(Ev& __ev) { std::get_scheduler_allocator(__ev); };

template<class Sch, class Ev>
concept __start_scheduler_is_exact = __has_start_scheduler<Ev> &&
    std::same_as<Sch, std::remove_cvref_t<decltype(
        std::execution::get_start_scheduler(std::declval<Ev&>()))>>;

template<class Sch, class Ev>
concept __start_scheduler_from_both = __has_start_scheduler<Ev> && __has_scheduler_allocator<Ev> &&
    requires(Ev& __ev) {
        Sch(std::execution::get_start_scheduler(__ev), std::get_scheduler_allocator(__ev));
    };

template<class Sch, class Ev>
concept __start_scheduler_from_scheduler = __has_start_scheduler<Ev> &&
    requires(Ev& __ev) { Sch(std::execution::get_start_scheduler(__ev)); };

template<class Sch, class Ev>
concept __start_scheduler_from_allocator = __has_scheduler_allocator<Ev> &&
    requires(Ev& __ev) { Sch(std::get_scheduler_allocator(__ev)); };

// The first viable of the five initializers, in the draft's order. Only the
// operands of the selected one are evaluated, and a selected construction
// that throws does not fall through to a later one.
enum class __start_arm { __is_exact, __from_both, __from_scheduler, __from_allocator, __from_nothing };

template<class Sch, class Ev>
consteval __start_arm __start_arm_of() {
    if constexpr (__start_scheduler_is_exact<Sch, Ev>) {
        return __start_arm::__is_exact;
    } else if constexpr (__start_scheduler_from_both<Sch, Ev>) {
        return __start_arm::__from_both;
    } else if constexpr (__start_scheduler_from_scheduler<Sch, Ev>) {
        return __start_arm::__from_scheduler;
    } else if constexpr (__start_scheduler_from_allocator<Sch, Ev>) {
        return __start_arm::__from_allocator;
    } else {
        return __start_arm::__from_nothing;
    }
}

// An object of the operation initialized from the result of a call: a
// prvalue is materialized here without a copy or move, and a reference stays
// that reference.
template<class T>
struct __retained {
    T __value;

    template<class Make>
    explicit __retained(Make&& __make) : __value(std::forward<Make>(__make)()) {}
};

// [task.state]/4 and /6. ev is the object get_env(rcvr). The operation keeps
// it for the forms that query it, after the frame and after SCHED and for as
// long as the operation, so an answer that refers into the environment stays
// valid. A prvalue answer is not kept: it initializes the constructor's
// parameter directly, as in the draft's expression. Each selected operand is
// evaluated once. S() queries nothing and keeps nothing.
template<class Rcvr>
struct __retained_ev {
    using __ev_expr_t = decltype(std::execution::get_env(std::declval<Rcvr&>()));
    using __ev_t = std::remove_reference_t<__ev_expr_t>;

    std::optional<__retained<__ev_expr_t>> __storage;

    __ev_t& __materialize(Rcvr& __rcvr) {
        __storage.emplace([&__rcvr]() -> __ev_expr_t { return std::execution::get_env(__rcvr); });
        return __storage->__value;
    }
};

// The selected expression yields a scheduler object, which initializes SCHED
// in the operation. start_scheduler_type must be an object type (see the
// class). SCHED itself has that type with its cv-qualification, so a copy
// made from it sees the same qualification as a copy from the alias would.
template<class Sch>
using __sched_object_t = std::remove_cv_t<Sch>;

template<class Sch, class Rcvr,
         __start_arm = __start_arm_of<Sch, typename __retained_ev<Rcvr>::__ev_t>()>
struct __start_operands {
    __sched_object_t<Sch> __select(Rcvr&) {
        static_assert(__value_initializable<Sch>,
                      "task: no start scheduler can be made for this receiver environment");
        return Sch();
    }
};

template<class Sch, class Rcvr>
struct __start_operands<Sch, Rcvr, __start_arm::__is_exact> {
    __retained_ev<Rcvr> __ev_;

    __sched_object_t<Sch> __select(Rcvr& __rcvr) {
        auto& __ev = __ev_.__materialize(__rcvr);
        return std::execution::get_start_scheduler(__ev);
    }
};

template<class Sch, class Rcvr>
struct __start_operands<Sch, Rcvr, __start_arm::__from_both> {
    __retained_ev<Rcvr> __ev_;

    __sched_object_t<Sch> __select(Rcvr& __rcvr) {
        auto& __ev = __ev_.__materialize(__rcvr);
        return Sch(std::execution::get_start_scheduler(__ev), std::get_scheduler_allocator(__ev));
    }
};

template<class Sch, class Rcvr>
struct __start_operands<Sch, Rcvr, __start_arm::__from_scheduler> {
    __retained_ev<Rcvr> __ev_;

    __sched_object_t<Sch> __select(Rcvr& __rcvr) {
        auto& __ev = __ev_.__materialize(__rcvr);
        return Sch(std::execution::get_start_scheduler(__ev));
    }
};

template<class Sch, class Rcvr>
struct __start_operands<Sch, Rcvr, __start_arm::__from_allocator> {
    __retained_ev<Rcvr> __ev_;

    __sched_object_t<Sch> __select(Rcvr& __rcvr) {
        auto& __ev = __ev_.__materialize(__rcvr);
        return Sch(std::get_scheduler_allocator(__ev));
    }
};

// [task.promise]/12, second item, and the Mandate of [task.members]/5.
template<class Alloc, class Rcvr>
concept __allocator_from_receiver = requires(Rcvr& __rcvr) {
    Alloc(std::get_allocator(std::execution::get_env(__rcvr)));
};

template<class Alloc, class Rcvr>
Alloc __receiver_allocator(Rcvr& __rcvr) noexcept {
    if constexpr (__allocator_from_receiver<Alloc, Rcvr>) {
        return Alloc(std::get_allocator(std::execution::get_env(__rcvr)));
    } else {
        return Alloc();
    }
}

// [task.promise]/12, fourth item: any query other than the three reserved
// ones goes to the Environment, with its arguments, if it is a forwarding
// query and the Environment answers it. The query keeps the type and category
// it was supplied with, in the classification as in the call, as it does in
// the forwarding views ([exec.fwd.env]).
template<class Query>
concept __reserved_query = std::same_as<Query, get_start_scheduler_t> ||
    std::same_as<Query, std::get_allocator_t> || std::same_as<Query, std::get_stop_token_t>;

template<class Env, class Query, class... Args>
concept __environment_queryable = requires(Env& __environment, Query&& __query, Args&&... __args) {
    __environment.query(static_cast<Query&&>(__query), static_cast<Args&&>(__args)...);
};

template<class Env, class Query, class... Args>
concept __nothrow_environment_queryable = requires(Env& __environment, Query&& __query, Args&&... __args) {
    { __environment.query(static_cast<Query&&>(__query), static_cast<Args&&>(__args)...) } noexcept;
};

// The classification inspects the query's class, so it comes last, and only
// for a complete type: a query the Environment does not answer, or an opaque
// one, is rejected before any class trait sees it.
template<class Query>
concept __complete_query = requires { sizeof(Query); };

template<class Env, class Query, class... Args>
concept __environment_answers = (!__reserved_query<std::remove_cvref_t<Query>>) &&
    __environment_queryable<Env, Query, Args...> &&
    __complete_query<std::remove_cvref_t<Query>> &&
    __forge_env_detail::__qualified_fwd_env_query<Query>;

} // namespace __forge_task

template<class T, class Environment>
class task {
    static_assert(std::same_as<T, void> || std::is_reference_v<T> ||
                      (std::is_object_v<T> && !std::is_array_v<T> &&
                       std::same_as<T, std::remove_cv_t<T>>),
                  "task<T, E>: T must be void, a reference or a cv-unqualified non-array object type");
    static_assert(std::is_class_v<Environment>, "task<T, E>: E must be a class type");

public:
    using sender_concept = sender_tag;
    using allocator_type = typename __forge_task::__allocator_of<Environment>::type;
    using start_scheduler_type = typename __forge_task::__start_scheduler_of<Environment>::type;
    // A reference start_scheduler_type is not supported. Read literally,
    // SCHED would be a reference initialized from the selected expression,
    // which dangles for a prvalue answer, and no general test decides when
    // keeping a reference is safe. This is stricter than the draft.
    static_assert(!std::is_reference_v<start_scheduler_type>,
                  "task<T, E>: E::start_scheduler_type must be an object type; a reference scheduler "
                  "type is not supported by this backport");
    using stop_source_type = typename __forge_task::__stop_source_of<Environment>::type;
    using stop_token_type = decltype(std::declval<stop_source_type>().get_token());
    using error_types = typename __forge_task::__error_types_of<Environment>::type;

    // [task.promise]
    class promise_type;

private:
    using __error_list_t = __forge_task::__error_list<error_types>;
    static_assert(__error_list_t::__valid_list,
                  "task<T, E>: error_types must be completion_signatures<set_error_t(E)...>");
    using __errors = typename __error_list_t::template __traits<promise_type, T>;
    using __core_t = typename __errors::__core;

    // [task.state]. The members follow the exposition order.
    template<receiver Rcvr>
    class __state final : public __core_t {
        using __rcvr_t = std::remove_cvref_t<Rcvr>;
        using __own_env_t = typename __forge_task::__own_env_of<
            Environment, decltype(std::execution::get_env(std::declval<Rcvr>()))>::type;

    public:
        using operation_state_concept = operation_state_tag;

        // The base adopts the frame before any member below can throw.
        template<class R>
        __state(std::coroutine_handle<promise_type> __handle, R&& __rr)
            : __core_t(__handle),
              __rcvr_(std::forward<R>(__rr)),
              __start_(),
              __sched_(),
              __own_env_(__forge_task::__make_own_env<__own_env_t>(__rcvr_)),
              __environment_(__forge_task::__make_environment<Environment>(__own_env_, __rcvr_)) {
            this->__outcome_ = std::addressof(__outcome_storage_);
        }

        // [task.state]/3: the frame goes first, before the members.
        ~__state() {
            this->__frame_.__destroy();
        }

        void start() & noexcept {
            promise_type& __promise = this->__frame_.__get().promise();
            __promise.__core_ = this;
            __promise.__state_ = this;
            __promise.__allocator_of_ = &__query_allocator;
            __promise.__environment_ = std::addressof(__environment_);
            // SCHED is initialized by the selected expression itself, in the
            // operation. The promise borrows it.
            __sched_.emplace([this]() -> __forge_task::__sched_object_t<start_scheduler_type> {
                return __start_.__select(__rcvr_);
            });
            __promise.__scheduler_ = std::addressof(__sched_->__value);
            // The operation may be gone when this returns.
            this->__resume();
        }

    private:
        // The frame, and every child in it, is gone.
        void __deliver_result() noexcept override {
            __errors::__send(__rcvr_, __outcome_storage_);
        }

        void __deliver_stopped() noexcept override {
            __forge_task::__send_stopped(__rcvr_);
        }

        void __deliver_error(std::size_t __index, void* __referent) noexcept override {
            __errors::__send(__rcvr_, __index, __referent);
        }

        // The promise does not know Rcvr. It reaches the query that depends
        // on the receiver through this.
        static allocator_type __query_allocator(void* __self) noexcept {
            return __forge_task::__receiver_allocator<allocator_type>(static_cast<__state*>(__self)->__rcvr_);
        }

        // Not [[no_unique_address]]: each environment is initialized from a
        // prvalue, which a potentially-overlapping member would have to copy.
        __rcvr_t __rcvr_;
        // ev, kept until the operation ends.
        __forge_task::__start_operands<start_scheduler_type, __rcvr_t> __start_;
        // SCHED. It lives in the operation and not in the frame, so that it
        // outlives the frame through the terminal's conversion of a yielded
        // error, and it is destroyed before ev.
        std::optional<__forge_task::__retained<start_scheduler_type>> __sched_;
        __own_env_t __own_env_;
        Environment __environment_;
        __forge_task::__outcome<T> __outcome_storage_;
    };

public:
    task(task&&) noexcept = default;
    ~task() = default;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() {
        if constexpr (std::is_void_v<T>) {
            return typename __error_list_t::template __signatures<set_value_t()>();
        } else {
            return typename __error_list_t::template __signatures<set_value_t(T)>();
        }
    }

    // [task.members]/6: the task owns a frame.
    template<receiver Rcvr>
    __state<Rcvr> connect(Rcvr&& __rcvr) && {
        static_assert(__forge_task::__allocator_from_receiver<allocator_type, std::remove_cvref_t<Rcvr>> ||
                          __forge_task::__value_initializable<allocator_type>,
                      "task: allocator_type needs get_allocator(get_env(rcvr)) or default construction");
        return __state<Rcvr>(__frame_.__release(), std::forward<Rcvr>(__rcvr));
    }

private:
    explicit task(std::coroutine_handle<promise_type> __handle) noexcept : __frame_(__handle) {}

    __forge_task::__frame_owner<promise_type> __frame_;
};

template<class T, class Environment>
class task<T, Environment>::promise_type : public __forge_task::__promise_return<T, __core_t> {
    // [task.promise]/12.
    struct __env_t {
        const promise_type* __promise_;

        // The reserved queries, for exactly these tag types. A type that only
        // converts to one of them is not that query. The stop token query is
        // reserved too and has no answer yet.
        template<class Query>
            requires std::same_as<std::remove_cvref_t<Query>, get_start_scheduler_t>
        start_scheduler_type query(Query&&) const noexcept {
            return start_scheduler_type(*__promise_->__scheduler_);
        }

        template<class Query>
            requires std::same_as<std::remove_cvref_t<Query>, std::get_allocator_t>
        allocator_type query(Query&&) const noexcept {
            return __promise_->__allocator_of_(__promise_->__state_);
        }

        template<class Query, class... Args>
            requires __forge_task::__environment_answers<Environment, Query, Args...>
        decltype(auto) query(Query&& __query, Args&&... __args) const
            noexcept(__forge_task::__nothrow_environment_queryable<Environment, Query, Args...>) {
            return __promise_->__environment_->query(static_cast<Query&&>(__query),
                                                     static_cast<Args&&>(__args)...);
        }
    };

public:
    // SCHED(*this), the scheduler object in the connected operation. Set by
    // start() before the body first runs.
    const start_scheduler_type* __scheduler_ = nullptr;
    // The connected operation and its receiver-dependent query.
    void* __state_ = nullptr;
    allocator_type (*__allocator_of_)(void*) noexcept = nullptr;
    Environment* __environment_ = nullptr;

    task get_return_object() noexcept {
        return task(std::coroutine_handle<promise_type>::from_promise(*this));
    }

    static constexpr std::suspend_always initial_suspend() noexcept {
        return {};
    }

    auto final_suspend() noexcept {
        return __forge_task::__final_awaiter<__core_t>{this->__core_};
    }

    void unhandled_exception() {
        if constexpr (__errors::__has_exception_ptr) {
            this->__core_->__outcome_->__exception = std::current_exception();
        } else {
            std::terminate();
        }
    }

    // The frame, and with it *this, is destroyed inside the completion.
    std::coroutine_handle<> unhandled_stopped() noexcept {
        __core_t* const __core = this->__core_;
        __core->__complete_stopped();
        return std::noop_coroutine();
    }

    template<class E>
    auto yield_value(with_error<E> __yielded) {
        using __payload_t = typename with_error<E>::type;
        constexpr bool __reachable = __errors::template __reachable<__payload_t>;
        static_assert(__reachable,
                      "co_yield with_error: the error converts to none of the task's error types");
        // Forge stages the payload with one extra move before the frame dies.
        static_assert(std::is_move_constructible_v<__payload_t>,
                      "co_yield with_error: the error payload must be move constructible");
        if constexpr (__reachable) {
            static_assert(__errors::template __unique_target<__payload_t>,
                          "co_yield with_error: the error converts to several of the task's error types");
            using __target_t = typename __errors::template __target<__payload_t>;
            return __forge_task::__error_awaiter<__core_t, __payload_t, __target_t>(
                this->__core_, std::move(__yielded.error));
        }
    }

    template<sender Sender>
    auto await_transform(Sender&& __sndr) {
        if constexpr (std::same_as<inline_scheduler, start_scheduler_type>) {
            return std::execution::as_awaitable(std::forward<Sender>(__sndr), *this);
        } else {
            return std::execution::as_awaitable(
                std::execution::affine(std::forward<Sender>(__sndr)), *this);
        }
    }

    auto get_env() const noexcept -> __env_t {
        return {this};
    }

    // [task.promise]/13: the default frame uses allocator_type().
    void* operator new(std::size_t __size) {
        return operator new(__size, std::allocator_arg, allocator_type());
    }

    // [task.promise]/14 to /17: a leading allocator_arg and allocator, after
    // the implicit object parameter for a member coroutine.
    template<class Alloc, class... Args>
    void* operator new(std::size_t __size, std::allocator_arg_t, Alloc __alloc, Args&&...) {
        return __forge_task::__allocate_frame(__size, __alloc);
    }

    template<class This, class Alloc, class... Args>
    void* operator new(std::size_t __size, const This&, std::allocator_arg_t, Alloc __alloc, Args&&...) {
        return __forge_task::__allocate_frame(__size, __alloc);
    }

    // [task.promise]/18 and /19.
    void operator delete(void* __pointer, std::size_t __size) noexcept {
        __forge_task::__deallocate_frame(__pointer, __size);
    }
};

} // namespace std::execution

#endif // __cpp_impl_coroutine
