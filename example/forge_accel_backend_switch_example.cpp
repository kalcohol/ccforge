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

#include <forge/accel.hpp>

#include <cassert>
#include "example_support.hpp"
#include <execution>
#include <span>
#include <utility>
#include <vector>

struct mock_backend {
    using context = forge::accel::mock::context;
    using queue = forge::accel::mock::queue;

    template<class T>
    using device_buffer = forge::accel::mock::device_buffer<T>;

    [[nodiscard]] static auto make_context() -> context {
        return context{forge::accel::mock::context_options{
            .thread_count = 1,
            .queue_capacity = 8,
        }};
    }

    [[nodiscard]] static auto get_queue(context& ctx) -> queue {
        return ctx.get_queue(forge::accel::queue_kind::compute);
    }

    template<class T>
    [[nodiscard]] static auto copy_to_device(
        queue& q,
        device_buffer<T>& dst,
        std::span<const T> src) {
        return forge::accel::mock::copy_to_device(q, dst, src);
    }

    template<class T>
    [[nodiscard]] static auto copy_to_host(
        queue& q,
        std::span<T> dst,
        device_buffer<T>& src) {
        return forge::accel::mock::copy_to_host(q, dst, src);
    }

    template<class Fn>
    [[nodiscard]] static auto submit(queue& q, Fn&& fn) {
        return forge::accel::mock::submit(q, std::forward<Fn>(fn));
    }
};

struct cpu_backend {
    using context = forge::accel::cpu::context;
    using queue = forge::accel::cpu::queue;

    template<class T>
    using device_buffer = forge::accel::cpu::device_buffer<T>;

    [[nodiscard]] static auto make_context() -> context {
        return context{forge::accel::cpu::context_options{
            .thread_count = 1,
            .queue_capacity = 8,
        }};
    }

    [[nodiscard]] static auto get_queue(context& ctx) -> queue {
        return ctx.get_queue(forge::accel::queue_kind::compute);
    }

    template<class T>
    [[nodiscard]] static auto copy_to_device(
        queue& q,
        device_buffer<T>& dst,
        std::span<const T> src) {
        return forge::accel::cpu::copy_to_device(q, dst, src);
    }

    template<class T>
    [[nodiscard]] static auto copy_to_host(
        queue& q,
        std::span<T> dst,
        device_buffer<T>& src) {
        return forge::accel::cpu::copy_to_host(q, dst, src);
    }

    template<class Fn>
    [[nodiscard]] static auto submit(queue& q, Fn&& fn) {
        return forge::accel::cpu::submit(q, std::forward<Fn>(fn));
    }
};

template<class Backend>
[[nodiscard]] auto run_backend(std::span<const float> input) -> std::vector<float> {
    auto ctx = Backend::make_context();
    auto q = Backend::get_queue(ctx);
    typename Backend::template device_buffer<float> device{ctx, input.size()};
    std::vector<float> output(input.size());

    forge_example::require(std::execution::sync_wait(
        Backend::copy_to_device(q, device, input)).has_value());

    forge_example::require(std::execution::sync_wait(
        Backend::submit(q, [&] {
            for (auto& value : device.span()) {
                value = value * 4.0f - 1.0f;
            }
        })).has_value());

    forge_example::require(std::execution::sync_wait(
        Backend::copy_to_host(q, std::span<float>{output}, device)).has_value());

    ctx.wait();
    return output;
}

int main() {
    std::vector<float> input{1.0f, 2.0f, 3.0f, 4.0f};
    auto mock_output = run_backend<mock_backend>(std::span<const float>{input});
    auto cpu_output = run_backend<cpu_backend>(std::span<const float>{input});

    forge_example::require((mock_output == std::vector<float>{3.0f, 7.0f, 11.0f, 15.0f}));
    forge_example::require(cpu_output == mock_output);
}
