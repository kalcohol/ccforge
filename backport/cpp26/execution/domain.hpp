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

#include "detail.hpp"
#include "env.hpp"
#include "concepts.hpp"

namespace std::execution {

// ──────────────────────────────────────────────────────────────────────────
// default_domain — [exec.domain.default]
// ──────────────────────────────────────────────────────────────────────────

struct default_domain {
    // transform_sender: default identity (pass-through)
    template<class Env, class Sender>
    static Sender&& transform_sender(Sender&& sndr, const Env&) noexcept {
        return static_cast<Sender&&>(sndr);
    }

    // transform_env: default identity
    template<class Env>
    static Env&& transform_env(const auto&, Env&& env) noexcept {
        return static_cast<Env&&>(env);
    }

    bool operator==(const default_domain&) const noexcept = default;
};

// ──────────────────────────────────────────────────────────────────────────
// get_domain CPO — [exec.get.domain]
// ──────────────────────────────────────────────────────────────────────────

struct get_domain_t {
    template<class Env>
        requires __forge_detail::tag_invocable<get_domain_t, const Env&>
    auto operator()(const Env& env) const noexcept
        -> __forge_detail::tag_invoke_result_t<get_domain_t, const Env&> {
        return __forge_detail::tag_invoke_fn(*this, env);
    }

    // Default: return default_domain when no tag_invoke specialization exists
    template<class Env>
        requires (!__forge_detail::tag_invocable<get_domain_t, const Env&>)
    default_domain operator()(const Env&) const noexcept {
        return {};
    }
};

inline constexpr get_domain_t get_domain{};

// Helper: get the domain from a sender's env
template<class Sender, class Env = empty_env>
using sender_domain_t = decltype(get_domain(
    std::execution::get_env(std::declval<Sender>())));

} // namespace std::execution
