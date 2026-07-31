// MIT License
//
// Copyright (c) 2026 Forge Project
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

#include <compare>
#include <type_traits>

namespace std {

#if defined(FORGE_FORCE_CONSTANT_WRAPPER_BACKPORT) || \
    (!defined(FORGE_HAS_NATIVE_CONSTANT_WRAPPER) && !defined(__cpp_lib_constant_wrapper))

template <auto X, class T = decltype(X)>
struct constant_wrapper;

template <auto X>
using __forge_cw_result = constant_wrapper<X, decltype(X)>;

#define FORGE_CW_RESULT(...)                                                    \
    __forge_cw_result<(__VA_ARGS__)>

template <class T>
struct __forge_is_constant_wrapper : false_type {};

template <auto X, class T>
struct __forge_is_constant_wrapper<constant_wrapper<X, T>> : true_type {};

template <class T>
concept __forge_constant_wrapper =
    __forge_is_constant_wrapper<remove_cvref_t<T>>::value;

template <class T>
inline constexpr bool __forge_cw_comparison_category =
    is_same_v<remove_cvref_t<T>, strong_ordering> ||
    is_same_v<remove_cvref_t<T>, weak_ordering> ||
    is_same_v<remove_cvref_t<T>, partial_ordering>;

template <class T>
inline constexpr bool __forge_cw_is_reference_wrapper =
    !is_same_v<unwrap_reference_t<remove_cvref_t<T>>, remove_cvref_t<T>>;

template <class T>
struct __forge_cw_member_pointer_class;

template <class M, class C>
struct __forge_cw_member_pointer_class<M C::*> {
    using type = C;
};

template <class F>
using __forge_cw_member_pointer_class_t =
    typename __forge_cw_member_pointer_class<remove_cvref_t<F>>::type;

template <class F, class... Args>
    requires (!is_member_pointer_v<remove_cvref_t<F>> &&
              requires(F&& fn, Args&&... args) {
                  static_cast<F&&>(fn)(static_cast<Args&&>(args)...);
              })
constexpr decltype(auto) __forge_cw_invoke(F&& fn, Args&&... args)
    noexcept(noexcept(static_cast<F&&>(fn)(static_cast<Args&&>(args)...))) {
    return static_cast<F&&>(fn)(static_cast<Args&&>(args)...);
}

template <class F, class T, class... Args>
    requires (is_member_function_pointer_v<remove_cvref_t<F>> &&
              is_base_of_v<__forge_cw_member_pointer_class_t<F>,
                           remove_cvref_t<T>> &&
              requires(F&& fn, T&& target, Args&&... args) {
                  (static_cast<T&&>(target).*static_cast<F&&>(fn))(
                      static_cast<Args&&>(args)...);
              })
constexpr decltype(auto) __forge_cw_invoke(F&& fn,
                                            T&& target,
                                            Args&&... args)
    noexcept(noexcept((static_cast<T&&>(target).*static_cast<F&&>(fn))(
        static_cast<Args&&>(args)...))) {
    return (static_cast<T&&>(target).*static_cast<F&&>(fn))(
        static_cast<Args&&>(args)...);
}

template <class F, class T, class... Args>
    requires (is_member_function_pointer_v<remove_cvref_t<F>> &&
              __forge_cw_is_reference_wrapper<T> &&
              requires(F&& fn, T&& target, Args&&... args) {
                  (static_cast<T&&>(target).get().*static_cast<F&&>(fn))(
                      static_cast<Args&&>(args)...);
              })
constexpr decltype(auto) __forge_cw_invoke(F&& fn,
                                            T&& target,
                                            Args&&... args)
    noexcept(noexcept((static_cast<T&&>(target).get().*static_cast<F&&>(fn))(
        static_cast<Args&&>(args)...))) {
    return (static_cast<T&&>(target).get().*static_cast<F&&>(fn))(
        static_cast<Args&&>(args)...);
}

template <class F, class T, class... Args>
    requires (is_member_function_pointer_v<remove_cvref_t<F>> &&
              !is_base_of_v<__forge_cw_member_pointer_class_t<F>,
                            remove_cvref_t<T>> &&
              !__forge_cw_is_reference_wrapper<T> &&
              requires(F&& fn, T&& target, Args&&... args) {
                  ((*static_cast<T&&>(target)).*static_cast<F&&>(fn))(
                      static_cast<Args&&>(args)...);
              })
constexpr decltype(auto) __forge_cw_invoke(F&& fn,
                                            T&& target,
                                            Args&&... args)
    noexcept(noexcept(((*static_cast<T&&>(target)).*static_cast<F&&>(fn))(
        static_cast<Args&&>(args)...))) {
    return ((*static_cast<T&&>(target)).*static_cast<F&&>(fn))(
        static_cast<Args&&>(args)...);
}

template <class F, class T>
    requires (is_member_object_pointer_v<remove_cvref_t<F>> &&
              is_base_of_v<__forge_cw_member_pointer_class_t<F>,
                           remove_cvref_t<T>> &&
              requires(F&& fn, T&& target) {
                  static_cast<T&&>(target).*static_cast<F&&>(fn);
              })
constexpr decltype(auto) __forge_cw_invoke(F&& fn, T&& target)
    noexcept(noexcept(static_cast<T&&>(target).*static_cast<F&&>(fn))) {
    return static_cast<T&&>(target).*static_cast<F&&>(fn);
}

template <class F, class T>
    requires (is_member_object_pointer_v<remove_cvref_t<F>> &&
              __forge_cw_is_reference_wrapper<T> &&
              requires(F&& fn, T&& target) {
                  static_cast<T&&>(target).get().*static_cast<F&&>(fn);
              })
constexpr decltype(auto) __forge_cw_invoke(F&& fn, T&& target)
    noexcept(noexcept(static_cast<T&&>(target).get().*static_cast<F&&>(fn))) {
    return static_cast<T&&>(target).get().*static_cast<F&&>(fn);
}

template <class F, class T>
    requires (is_member_object_pointer_v<remove_cvref_t<F>> &&
              !is_base_of_v<__forge_cw_member_pointer_class_t<F>,
                            remove_cvref_t<T>> &&
              !__forge_cw_is_reference_wrapper<T> &&
              requires(F&& fn, T&& target) {
                  (*static_cast<T&&>(target)).*static_cast<F&&>(fn);
              })
constexpr decltype(auto) __forge_cw_invoke(F&& fn, T&& target)
    noexcept(noexcept((*static_cast<T&&>(target)).*static_cast<F&&>(fn))) {
    return (*static_cast<T&&>(target)).*static_cast<F&&>(fn);
}

template <class T>
consteval bool __forge_cw_constant_expression(T) noexcept {
    return true;
}

template <class T>
concept __forge_constexpr_param = requires {
    typename bool_constant<__forge_cw_constant_expression(
        remove_cvref_t<T>::value)>;
    requires (!__forge_cw_comparison_category<
              decltype(remove_cvref_t<T>::value)>);
} && (__forge_constant_wrapper<T> || requires {
    typename __forge_cw_result<
        remove_cvref_t<T>::value>;
});

struct __forge_cw_operators {
    template <__forge_constexpr_param T>
        requires requires(remove_cvref_t<typename T::value_type> value) { ++value; }
    constexpr auto operator++(this T) noexcept {
        return FORGE_CW_RESULT([] {
            auto value = T::value;
            return ++value;
        }()){};
    }

    template <__forge_constexpr_param T>
        requires requires(remove_cvref_t<typename T::value_type> value) { value++; }
    constexpr auto operator++(this T, int) noexcept {
        return FORGE_CW_RESULT([] {
            auto value = T::value;
            return value++;
        }()){};
    }

    template <__forge_constexpr_param T>
        requires requires(remove_cvref_t<typename T::value_type> value) { --value; }
    constexpr auto operator--(this T) noexcept {
        return FORGE_CW_RESULT([] {
            auto value = T::value;
            return --value;
        }()){};
    }

    template <__forge_constexpr_param T>
        requires requires(remove_cvref_t<typename T::value_type> value) { value--; }
    constexpr auto operator--(this T, int) noexcept {
        return FORGE_CW_RESULT([] {
            auto value = T::value;
            return value--;
        }()){};
    }

#define FORGE_CW_COMPOUND_OPERATOR(op)                                             \
    template <__forge_constexpr_param T, __forge_constexpr_param R>                \
        requires requires(remove_cvref_t<typename T::value_type> value) {           \
            value op R::value;                                                      \
        }                                                                           \
    constexpr auto operator op(this T, R) noexcept {                               \
        return FORGE_CW_RESULT([] {                                                \
            auto value = T::value;                                                  \
            return value op R::value;                                               \
        }()){};                                                                      \
    }

    FORGE_CW_COMPOUND_OPERATOR(+=)
    FORGE_CW_COMPOUND_OPERATOR(-=)
    FORGE_CW_COMPOUND_OPERATOR(*=)
    FORGE_CW_COMPOUND_OPERATOR(/=)
    FORGE_CW_COMPOUND_OPERATOR(%=)
    FORGE_CW_COMPOUND_OPERATOR(&=)
    FORGE_CW_COMPOUND_OPERATOR(|=)
    FORGE_CW_COMPOUND_OPERATOR(^=)
    FORGE_CW_COMPOUND_OPERATOR(<<=)
    FORGE_CW_COMPOUND_OPERATOR(>>=)

#undef FORGE_CW_COMPOUND_OPERATOR
};

template <auto X, class T>
struct constant_wrapper : __forge_cw_operators {
    static constexpr decltype(auto) value = (X);
    using type = constant_wrapper;
    using value_type = decltype(X);

    static_assert(is_same_v<T, value_type>,
                  "constant_wrapper's second template argument must match decltype(X)");
    static_assert(!__forge_cw_comparison_category<value_type>,
                  "constant_wrapper requires a structural value type");

    template <__forge_constexpr_param R>
        requires requires(remove_cvref_t<value_type> result) { result = R::value; }
    constexpr auto operator=(R) const noexcept {
        return FORGE_CW_RESULT([] {
            auto result = value;
            return result = R::value;
        }()){};
    }

    constexpr operator decltype(value)() const noexcept {
        return value;
    }

private:
    template <class... Args>
    static constexpr bool __constant_invocable = requires {
        requires (__forge_constexpr_param<remove_cvref_t<Args>> && ...);
        typename FORGE_CW_RESULT(
            __forge_cw_invoke(X, remove_cvref_t<Args>::value...));
    };

    template <class... Args>
    static constexpr bool __runtime_invocable = requires(Args&&... args) {
        __forge_cw_invoke(value, static_cast<Args&&>(args)...);
    };

    template <class... Args>
    static consteval bool __call_is_nothrow() {
        if constexpr (__constant_invocable<Args...>) {
            return true;
        } else {
            return noexcept(__forge_cw_invoke(value, declval<Args>()...));
        }
    }

    template <class... Args>
    static constexpr bool __constant_subscriptable = requires {
        requires (__forge_constexpr_param<remove_cvref_t<Args>> && ...);
        typename FORGE_CW_RESULT(
            X[remove_cvref_t<Args>::value...]);
    };

    template <class... Args>
    static constexpr bool __runtime_subscriptable = requires(Args&&... args) {
        value[static_cast<Args&&>(args)...];
    };

    template <class... Args>
    static consteval bool __subscript_is_nothrow() {
        if constexpr (__constant_subscriptable<Args...>) {
            return true;
        } else {
            return noexcept(value[declval<Args>()...]);
        }
    }

public:
    template <class... Args>
        requires (__constant_invocable<Args...> ||
                  __runtime_invocable<Args...>)
    static constexpr decltype(auto) operator()(Args&&... args)
        noexcept(__call_is_nothrow<Args...>()) {
        if constexpr (__constant_invocable<Args...>) {
            return FORGE_CW_RESULT(
                __forge_cw_invoke(X, remove_cvref_t<Args>::value...)){};
        } else {
            return __forge_cw_invoke(value, static_cast<Args&&>(args)...);
        }
    }

    template <class... Args>
        requires (__constant_subscriptable<Args...> ||
                  __runtime_subscriptable<Args...>)
    static constexpr decltype(auto) operator[](Args&&... args)
        noexcept(__subscript_is_nothrow<Args...>()) {
        if constexpr (__constant_subscriptable<Args...>) {
            return FORGE_CW_RESULT(
                X[remove_cvref_t<Args>::value...]){};
        } else {
            return value[static_cast<Args&&>(args)...];
        }
    }
};

#define FORGE_CW_UNARY_OPERATOR(op)                                      \
    template <auto X, class T>                                           \
    constexpr auto operator op(constant_wrapper<X, T>) noexcept          \
        -> FORGE_CW_RESULT(op X) {                                      \
        return {};                                                       \
    }

FORGE_CW_UNARY_OPERATOR(+)
FORGE_CW_UNARY_OPERATOR(-)
FORGE_CW_UNARY_OPERATOR(~)
FORGE_CW_UNARY_OPERATOR(!)
FORGE_CW_UNARY_OPERATOR(*)

#undef FORGE_CW_UNARY_OPERATOR

template <auto X, class T>
constexpr auto operator&(constant_wrapper<X, T>) noexcept
    -> FORGE_CW_RESULT(&constant_wrapper<X, T>::value) {
    return {};
}

#define FORGE_CW_BINARY_OPERATOR(op)                                           \
    template <auto X, class XT, auto Y, class YT>                              \
    constexpr auto operator op(constant_wrapper<X, XT>,                        \
                               constant_wrapper<Y, YT>) noexcept                \
        -> FORGE_CW_RESULT(X op Y) {                                           \
        return {};                                                              \
    }                                                                           \
                                                                                \
    template <auto X, class XT, __forge_constexpr_param R>                      \
        requires (!__forge_constant_wrapper<R>)                                 \
    constexpr auto operator op(constant_wrapper<X, XT>, R) noexcept             \
        -> FORGE_CW_RESULT(X op remove_cvref_t<R>::value) {                     \
        return {};                                                              \
    }                                                                           \
                                                                                \
    template <__forge_constexpr_param L, auto Y, class YT>                      \
        requires (!__forge_constant_wrapper<L>)                                 \
    constexpr auto operator op(L, constant_wrapper<Y, YT>) noexcept             \
        -> FORGE_CW_RESULT(remove_cvref_t<L>::value op Y) {                     \
        return {};                                                              \
    }

FORGE_CW_BINARY_OPERATOR(+)
FORGE_CW_BINARY_OPERATOR(-)
FORGE_CW_BINARY_OPERATOR(*)
FORGE_CW_BINARY_OPERATOR(/)
FORGE_CW_BINARY_OPERATOR(%)
FORGE_CW_BINARY_OPERATOR(<<)
FORGE_CW_BINARY_OPERATOR(>>)
FORGE_CW_BINARY_OPERATOR(&)
FORGE_CW_BINARY_OPERATOR(|)
FORGE_CW_BINARY_OPERATOR(^)
FORGE_CW_BINARY_OPERATOR(<)
FORGE_CW_BINARY_OPERATOR(<=)
FORGE_CW_BINARY_OPERATOR(==)
FORGE_CW_BINARY_OPERATOR(!=)
FORGE_CW_BINARY_OPERATOR(>)
FORGE_CW_BINARY_OPERATOR(>=)

#undef FORGE_CW_BINARY_OPERATOR

template <auto X, auto Y>
    requires requires { X ->* Y; }
consteval auto __forge_cw_member_access() {
    return X ->* Y;
}

template <auto X, class XT, auto Y, class YT>
    requires requires {
        typename FORGE_CW_RESULT(__forge_cw_member_access<X, Y>());
    }
constexpr auto operator->*(constant_wrapper<X, XT>,
                           constant_wrapper<Y, YT>) noexcept
    -> FORGE_CW_RESULT(__forge_cw_member_access<X, Y>()) {
    return {};
}

template <auto X, class XT, __forge_constexpr_param R>
    requires (!__forge_constant_wrapper<R> &&
              requires {
                  typename FORGE_CW_RESULT(__forge_cw_member_access<
                                           X,
                                           remove_cvref_t<R>::value>());
              })
constexpr auto operator->*(constant_wrapper<X, XT>, R) noexcept
    -> FORGE_CW_RESULT(__forge_cw_member_access<
                       X,
                       remove_cvref_t<R>::value>()) {
    return {};
}

template <__forge_constexpr_param L, auto Y, class YT>
    requires (!__forge_constant_wrapper<L> &&
              requires {
                  typename FORGE_CW_RESULT(__forge_cw_member_access<
                                           remove_cvref_t<L>::value,
                                           Y>());
              })
constexpr auto operator->*(L, constant_wrapper<Y, YT>) noexcept
    -> FORGE_CW_RESULT(__forge_cw_member_access<
                       remove_cvref_t<L>::value,
                       Y>()) {
    return {};
}

// MSVC accepts the standard comparison categories as NTTPs even though they
// are not structural types. Exclude them explicitly so the built-in fallback
// remains available and relational rewritten candidates do not shadow the
// constant-preserving overloads above.
template <auto X, class XT, auto Y, class YT>
    requires (!__forge_cw_comparison_category<decltype(X <=> Y)> &&
              requires { typename FORGE_CW_RESULT(X <=> Y); })
constexpr auto operator<=>(constant_wrapper<X, XT>,
                           constant_wrapper<Y, YT>) noexcept
    -> FORGE_CW_RESULT(X <=> Y) {
    return {};
}

template <auto X, class XT, __forge_constexpr_param R>
    requires (!__forge_constant_wrapper<R> &&
              !__forge_cw_comparison_category<
                  decltype(X <=> remove_cvref_t<R>::value)> &&
              requires {
                  typename FORGE_CW_RESULT(
                      X <=> remove_cvref_t<R>::value);
              })
constexpr auto operator<=>(constant_wrapper<X, XT>, R) noexcept
    -> FORGE_CW_RESULT(X <=> remove_cvref_t<R>::value) {
    return {};
}

template <__forge_constexpr_param L, auto Y, class YT>
    requires (!__forge_constant_wrapper<L> &&
              !__forge_cw_comparison_category<
                  decltype(remove_cvref_t<L>::value <=> Y)> &&
              requires {
                  typename FORGE_CW_RESULT(
                      remove_cvref_t<L>::value <=> Y);
              })
constexpr auto operator<=>(L, constant_wrapper<Y, YT>) noexcept
    -> FORGE_CW_RESULT(remove_cvref_t<L>::value <=> Y) {
    return {};
}

#define FORGE_CW_LOGICAL_OPERATOR(op)                                         \
    template <auto X, class XT, auto Y, class YT>                             \
        requires (!is_constructible_v<bool, decltype(X)> ||                   \
                  !is_constructible_v<bool, decltype(Y)>)                     \
    constexpr auto operator op(constant_wrapper<X, XT>,                       \
                               constant_wrapper<Y, YT>) noexcept               \
        -> FORGE_CW_RESULT(X op Y) {                                          \
        return {};                                                             \
    }                                                                          \
                                                                               \
    template <auto X, class XT, __forge_constexpr_param R>                     \
        requires (!__forge_constant_wrapper<R> &&                              \
                  (!is_constructible_v<bool, decltype(X)> ||                   \
                   !is_constructible_v<                                        \
                       bool, decltype(remove_cvref_t<R>::value)>))              \
    constexpr auto operator op(constant_wrapper<X, XT>, R) noexcept            \
        -> FORGE_CW_RESULT(X op remove_cvref_t<R>::value) {                    \
        return {};                                                             \
    }                                                                          \
                                                                               \
    template <__forge_constexpr_param L, auto Y, class YT>                     \
        requires (!__forge_constant_wrapper<L> &&                              \
                  (!is_constructible_v<                                        \
                       bool, decltype(remove_cvref_t<L>::value)> ||             \
                   !is_constructible_v<bool, decltype(Y)>))                     \
    constexpr auto operator op(L, constant_wrapper<Y, YT>) noexcept            \
        -> FORGE_CW_RESULT(remove_cvref_t<L>::value op Y) {                    \
        return {};                                                             \
    }

FORGE_CW_LOGICAL_OPERATOR(&&)
FORGE_CW_LOGICAL_OPERATOR(||)

#undef FORGE_CW_LOGICAL_OPERATOR

template <auto X, class XT, auto Y, class YT>
constexpr auto operator,(constant_wrapper<X, XT>,
                         constant_wrapper<Y, YT>) noexcept = delete;

template <auto X, class XT, __forge_constexpr_param R>
    requires (!__forge_constant_wrapper<R>)
constexpr auto operator,(constant_wrapper<X, XT>, R) noexcept = delete;

template <__forge_constexpr_param L, auto Y, class YT>
    requires (!__forge_constant_wrapper<L>)
constexpr auto operator,(L, constant_wrapper<Y, YT>) noexcept = delete;

template <auto X>
constexpr auto cw = constant_wrapper<X, decltype(X)>{};

#undef FORGE_CW_RESULT

#  if !defined(__cpp_lib_constant_wrapper)
#    define __cpp_lib_constant_wrapper 202606L
#  endif

#endif // forced or no native constant_wrapper

} // namespace std
