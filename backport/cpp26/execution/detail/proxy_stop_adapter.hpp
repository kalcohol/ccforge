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

#include <concepts>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "../stop_token.hpp"
#include "sync_shim.hpp"

namespace std::execution::__forge_parsched_detail {

// [stoptoken.concepts]/4 tests `tok.stop_possible()` on a const requires-
// parameter. That needs P2280, which Clang 19 rejects in this form. The
// replacement proxy uses only the portable static form, on every toolchain.
// The public unstoppable_token in stop_token.hpp adds the draft's form where
// the compiler is known to accept it; the proxy does not follow it. A token
// with only a non-static observer takes the adapter path and is ill-formed
// if its callback fails the proxy's static_assert.
template<class Token>
concept __proxy_static_stop_impossible = requires {
    requires std::bool_constant<(!Token::stop_possible())>::value;
};

template<class Token>
concept __proxy_unstoppable_token = std::stoppable_token<Token> &&
    __proxy_static_stop_impossible<Token>;

template<class Token, class Forwarder>
concept __proxy_stoppable_callback = std::__forge_stop_detail::__stoppable_token_for<Token, Forwarder> &&
    std::constructible_from<std::stop_callback_for_t<Token, Forwarder>, const Token&, Forwarder>;

enum class __proxy_stop_event {
    first_query, waiting, register_callback, publish, read_token,
    forward_request, terminal_commit, unregister_callback
};

struct __no_proxy_stop_observer {
    void operator()(__proxy_stop_event) const noexcept {}
};

using __proxy_stop_cell = std::optional<std::inplace_stop_source>;

template<class Observer, class Source = std::inplace_stop_source>
struct __proxy_stop_forwarder {
    std::shared_ptr<std::optional<Source>> __source;
    [[no_unique_address]] Observer __observe;

    void operator()() const noexcept {
        // A downstream callback can complete and delete the operation on
        // this very thread, including this upstream callback object.
        auto __pin = __source;
        __observe(__proxy_stop_event::forward_request);
        (*__pin)->request_stop();
    }
};

// The token a translated source issues, from the stored source as an lvalue.
template<class Source>
using __proxy_issued_token_t = std::remove_cvref_t<decltype(std::declval<Source&>().get_token())>;

// The requestable construction. Both standard sources make a fresh stop state
// this way. A source that cannot be made this way cannot translate a token
// that can stop.
template<class Source>
concept __proxy_requestable_source = requires { Source(); };

// std::nostopstate exists only where the library has std::stop_source.
#if defined(__cpp_lib_jthread)
template<class Source>
concept __proxy_no_stop_state_source = requires { Source(std::nostopstate); };

template<class Source>
void __proxy_emplace_no_stop_state(std::optional<Source>& __cell) {
    __cell.emplace(std::nostopstate);
}

template<class Token>
concept __proxy_standard_token = std::same_as<Token, std::stop_token>;
#else
template<class Source>
concept __proxy_no_stop_state_source = false;

template<class Source>
void __proxy_emplace_no_stop_state(std::optional<Source>&) {}

template<class Token>
concept __proxy_standard_token = false;
#endif

// A token default is examined as an answer only if it cannot throw. A
// potentially throwing one could end a query that another candidate answers.
template<class Token>
concept __proxy_default_token =
    (!std::same_as<Token, std::inplace_stop_token>) && std::is_nothrow_default_constructible_v<Token>;

// A token default whose value is known to be impossible without observing it.
template<class Token>
concept __proxy_sure_default_token =
    __proxy_default_token<Token> && (std::unstoppable_token<Token> || __proxy_standard_token<Token>);

// A second source slot. It exists only where a default constructed source
// must be examined while a source made from nostopstate is being held.
template<class Source, bool Present>
struct __proxy_spare_source {
    mutable std::optional<Source> __slot;
};

template<class Source>
struct __proxy_spare_source<Source, false> {};

// Token types whose value-initialized object is specified to be disengaged
// and equal to every other such object.
template<class Token>
concept __proxy_known_disengaged_token = std::same_as<Token, std::inplace_stop_token>;

// Whether the answer for an upstream that cannot stop needs no state at all.
template<class Source>
concept __proxy_answers_without_source = __proxy_known_disengaged_token<__proxy_issued_token_t<Source>>;

// Token types specified to allow concurrent copies of one object. Any other
// token is copied out of the adapter one reader at a time.
template<class Token>
concept __proxy_shareable_token = std::same_as<Token, std::inplace_stop_token> ||
    std::same_as<Token, std::never_stop_token> || __proxy_standard_token<Token>;

// What an examined candidate's token reports. Each observer is read once.
struct __proxy_observation {
    bool __possible;
    bool __requested;

    bool __impossible() const noexcept {
        return !__possible && !__requested;
    }
};

template<class Token>
__proxy_observation __proxy_observe(const Token& __token) noexcept {
    const bool __possible = __token.stop_possible();
    const bool __requested = __token.stop_requested();
    return {__possible, __requested};
}

// A reader for a row that never reads the receiver's token.
struct __proxy_no_read {};

// Where a source of the adapter lives. A forwarding callback pins the cell,
// so it is shared. A search for an upstream that can never stop keeps it in
// place. A row that never constructs a source has no storage for one, and
// does not need the source type to be destructible.
enum class __proxy_cell_kind { __shared, __local, __none };

template<class Source, __proxy_cell_kind Kind>
struct __proxy_source_cell {
    std::shared_ptr<std::optional<Source>> __shared = std::make_shared<std::optional<Source>>();

    std::optional<Source>& __get() const noexcept {
        return *__shared;
    }
};

template<class Source>
struct __proxy_source_cell<Source, __proxy_cell_kind::__local> {
    mutable std::optional<Source> __local;

    std::optional<Source>& __get() const noexcept {
        return __local;
    }
};

template<class Source>
struct __proxy_source_cell<Source, __proxy_cell_kind::__none> {};

// Whether a second source slot is needed. The availability of a later
// candidate is asked only where an earlier one does not already settle the
// answer, so a constructor that cannot even be tested is never looked at.
template<class Issued, class Source, bool Searches>
consteval bool __proxy_needs_spare() {
    if constexpr (!Searches) {
        return false;
    } else if constexpr (__proxy_sure_default_token<Issued>) {
        return false;
    } else if constexpr (std::unstoppable_token<Issued>) {
        return false;
    } else if constexpr (__proxy_no_stop_state_source<Source>) {
        return __proxy_requestable_source<Source>;
    } else {
        return false;
    }
}

// The upstream registration. An upstream that can never stop has none, and
// its callback type is not named.
template<class Token, class Forwarder, bool Forwards>
struct __proxy_callback_slot {
    std::optional<std::stop_callback_for_t<Token, Forwarder>> __registration;

    bool __remove() noexcept {
        if (!__registration) {
            return false;
        }
        __registration.reset();
        return true;
    }
};

template<class Token, class Forwarder>
struct __proxy_callback_slot<Token, Forwarder, false> {
    bool __remove() noexcept {
        return false;
    }
};

// The one issued token. Readers copy it from a const object. A user-defined
// token gives no guarantee for two concurrent copies of one object, so those
// copies are serialized. No callback, forward or completion runs under the
// lock.
template<class Token, bool Serialized>
struct __proxy_token_slot {
    std::optional<Token> __token;
    mutable __forge_sync::mutex __copying;

    Token __copy() const noexcept {
        __forge_sync::unique_lock __lock{__copying};
        return Token(*__token);
    }
};

template<class Token>
struct __proxy_token_slot<Token, false> {
    std::optional<Token> __token;

    Token __copy() const noexcept {
        return Token(*__token);
    }
};

// Protocol model: formal/proxy_stop_adapter. Specification: the W2 decision
// table of pilot batch G2b, summarized in the conformance ledger at ST-07.
// The source, the issued token and the registration are lazy. Reserve a
// shared source cell at operation construction, where allocation can throw,
// rather than inside the noexcept query. The initializer commits one answer
// before the forwarder can reach the source and before publication. Every
// query returns a copy of it, so no token factory is called concurrently.
//
// NeverStops is the caller's static classification of Token. With it the
// upstream is not polled, no source is constructed for a request and no
// forwarder callback type is named. The replacement proxy classifies with
// the portable static form before it instantiates the adapter and leaves
// this false. std::execution::task passes the public unstoppable_token
// concept.
template<class Token, class Observer = __no_proxy_stop_observer, class Source = std::inplace_stop_source,
         bool NeverStops = false>
class __proxy_stop_adapter {
    using __forward_stop = __proxy_stop_forwarder<Observer, Source>;
    using __issued_t = __proxy_issued_token_t<Source>;

    static constexpr bool __forwards = !NeverStops;
    // The receiver's token is itself an answer when it is of the issued type.
    static constexpr bool __same_type = std::same_as<Token, __issued_t>;
    // Otherwise the in-place token's value is, and only then are the
    // candidates searched: a token default, a source made from nostopstate
    // and a default constructed source.
    static constexpr bool __searches = !__same_type && !__proxy_known_disengaged_token<__issued_t>;
    // Every token of a type that can never stop is impossible unobserved.
    static constexpr bool __never_engaged = std::unstoppable_token<__issued_t>;
    static constexpr bool __has_spare = __proxy_needs_spare<__issued_t, Source, __searches>();
    // No storage where no source can be constructed: without a search, and
    // where a token default that is known to be impossible ends it.
    static constexpr __proxy_cell_kind __cell_kind =
        __forwards ? __proxy_cell_kind::__shared
                   : (__searches && !__proxy_sure_default_token<__issued_t>) ? __proxy_cell_kind::__local
                                                                              : __proxy_cell_kind::__none;

    enum : unsigned { __fresh, __initializing, __ready };

    [[no_unique_address]] Observer __observe_;
    [[no_unique_address]] __proxy_source_cell<Source, __cell_kind> __source_;
    [[no_unique_address]] __proxy_spare_source<Source, __has_spare> __spare_;
    mutable __proxy_token_slot<__issued_t, !__proxy_shareable_token<__issued_t>> __token_;
    mutable __proxy_callback_slot<Token, __forward_stop, __forwards> __callback_;
    mutable __forge_sync::atomic<unsigned> __phase_{__fresh};

    void __commit(const __issued_t& __answer) const noexcept {
        __token_.__token.emplace(__answer);
    }

    // T3 where no default constructed source is left to examine.
    [[noreturn]] void __exhausted() const noexcept {
        std::terminate();
    }

    // A default constructed source, examined in __slot. __held is the token
    // of an engaged, unrequested nostopstate source that the cell keeps, if
    // there is one. Whatever is not chosen is released token first.
    void __examine_default_source(std::optional<Source>& __slot, std::optional<__issued_t>& __held) const noexcept {
        __slot.emplace();
        std::optional<__issued_t> __candidate;
        __candidate.emplace(__slot->get_token());
        if constexpr (__never_engaged) {
            __commit(*__candidate);
        } else {
            const __proxy_observation __seen = __proxy_observe(*__candidate);
            if (__seen.__impossible()) {
                if (__held) {
                    __held.reset();
                    __source_.__get().reset();
                }
                __commit(*__candidate);
                return;
            }
            if (!__held && !__seen.__requested) {
                // Engaged and unrequested, and nothing was disengaged (ST-07.2).
                __commit(*__candidate);
                return;
            }
            __candidate.reset();
            __slot.reset();
            if (__held) {
                // The source found first stays the answer (ST-07.2).
                __commit(*__held);
                return;
            }
            // No candidate is usable without reporting a request.
            std::terminate();
        }
    }

    // The source candidates, after no token value gave the answer. A source
    // made from nostopstate comes first. With a token type that can never
    // stop its token is the answer unobserved, and a default constructed
    // source is not even asked about.
    void __examine_sources() const noexcept {
        if constexpr (__proxy_no_stop_state_source<Source>) {
            if constexpr (__never_engaged) {
                __proxy_emplace_no_stop_state(__source_.__get());
                __commit(__source_.__get()->get_token());
            } else {
                std::optional<Source>& __cell = __source_.__get();
                std::optional<__issued_t> __held;
                {
                    __proxy_emplace_no_stop_state(__cell);
                    // This copy of the candidate's token ends with the block,
                    // before the next candidate can replace its source.
                    std::optional<__issued_t> __candidate;
                    __candidate.emplace(__cell->get_token());
                    const __proxy_observation __seen = __proxy_observe(*__candidate);
                    if (__seen.__impossible()) {
                        __commit(*__candidate);
                        return;
                    }
                    if (__seen.__requested) {
                        __candidate.reset();
                        __cell.reset();
                    } else {
                        // Kept while the default constructed source is examined.
                        __held.emplace(*__candidate);
                    }
                }
                if constexpr (__has_spare) {
                    __examine_default_source(__held ? __spare_.__slot : __cell, __held);
                } else if (__held) {
                    __commit(*__held);
                } else {
                    __exhausted();
                }
            }
        } else {
            // X3: nothing before this answered, and this is the last candidate.
            static_assert(__proxy_default_token<__issued_t> || __proxy_requestable_source<Source>,
                          "stop adapter: the token has no nonthrowing default and the source has neither a "
                          "nostopstate nor a default constructor, so this pairing cannot be translated");
            if constexpr (__proxy_requestable_source<Source>) {
                std::optional<__issued_t> __held;
                __examine_default_source(__source_.__get(), __held);
            } else {
                // A token default was examined and was not impossible.
                __exhausted();
            }
        }
    }

    // The candidates for an upstream that cannot stop, in order. The first
    // whose token reports neither possible nor requested is the answer.
    // Otherwise the first engaged, unrequested token of a source constructed
    // here is (ST-07.2). A request is never fabricated. A token default that
    // is known to be impossible ends the search before any source
    // constructor is asked about.
    void __search() const noexcept {
        if constexpr (__proxy_sure_default_token<__issued_t>) {
            __token_.__token.emplace();
        } else {
            if constexpr (__proxy_default_token<__issued_t>) {
                const __issued_t __value = __issued_t();
                if (__proxy_observe(__value).__impossible()) {
                    __commit(__value);
                    return;
                }
            }
            __examine_sources();
        }
    }

    // The answer for an upstream that cannot stop. __sampled is the
    // receiver's own token where one was read.
    void __issue_impossible(const Token* __sampled) const noexcept {
        if constexpr (__same_type) {
            // The one sample of stop_possible() was false, or the type can
            // never stop: the token is impossible and is not polled again.
            __token_.__token.emplace(*__sampled);
        } else if constexpr (__searches) {
            __search();
        } else {
            __token_.__token.emplace(std::inplace_stop_token());
        }
    }

    template<class Read>
    void __initialize(Read&& __read) const noexcept {
        if constexpr (__forwards) {
            static_assert(__proxy_requestable_source<Source>,
                          "stop adapter: translating a token that can stop needs a default "
                          "constructible source");
            auto __upstream = std::forward<Read>(__read)();
            // The one observation of whether the upstream can stop.
            if (__upstream.stop_possible()) {
                std::optional<Source>& __cell = __source_.__get();
                __cell.emplace();
                // A default constructed source must hold a fresh stop state.
                // One that does not would lose every forwarded request.
                if (!__cell->stop_possible() || __cell->stop_requested()) {
                    std::terminate();
                }
                __token_.__token.emplace(__cell->get_token());
                __observe_(__proxy_stop_event::register_callback);
                // Registration itself handles an earlier or concurrent request.
                __callback_.__registration.emplace(
                    std::move(__upstream), __forward_stop{__source_.__shared, __observe_});
                return;
            }
            __issue_impossible(std::addressof(__upstream));
        } else if constexpr (__same_type) {
            // Statically impossible and of the issued type: read once, as
            // the answer. Nothing else is instantiated.
            const Token __held = std::forward<Read>(__read)();
            __issue_impossible(std::addressof(__held));
        } else {
            // The receiver's token is not read. __read is not invoked.
            __issue_impossible(nullptr);
        }
    }

public:
    explicit __proxy_stop_adapter(Observer __observe = {})
        : __observe_(std::move(__observe)) {}

    template<class Read>
    __issued_t __query(Read&& __read) const noexcept {
        unsigned __expected = __fresh;
        if (__phase_.compare_exchange_strong(__expected, __initializing)) {
            __observe_(__proxy_stop_event::first_query);
            __initialize(std::forward<Read>(__read));
            __observe_(__proxy_stop_event::publish);
            __phase_.store(__ready);
            __phase_.notify_all();
        } else if (__expected == __initializing) {
            __observe_(__proxy_stop_event::waiting);
            __phase_.wait(__initializing);
        }
        __observe_(__proxy_stop_event::read_token);
        return __token_.__copy();
    }

    // The backend ends all proxy queries and execute calls before terminal
    // dispatch. It need not serialize a pending upstream stop with this call.
    void __finish() noexcept {
        __observe_(__proxy_stop_event::terminal_commit);
        if (__callback_.__remove()) {
            __observe_(__proxy_stop_event::unregister_callback);
        }
    }
};

} // namespace std::execution::__forge_parsched_detail
