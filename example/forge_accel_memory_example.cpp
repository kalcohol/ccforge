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
#include <forge/execution.hpp>

#include <execution>
#include <algorithm>
#include <cassert>
#include "example_support.hpp"
#include <cstddef>
#include <initializer_list>

int main() {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();

    forge::accel::mock::host_byte_buffer staging{
        ctx,
        4,
        forge::accel::memory_kind::pinned_host};
    forge::accel::mock::device_byte_buffer bytes{ctx, 4};
    forge::accel::mock::host_byte_buffer readback{
        ctx,
        4,
        forge::accel::memory_kind::mapped_host};

    std::ranges::copy(
        std::initializer_list<std::byte>{
            std::byte{0x10},
            std::byte{0x20},
            std::byte{0x30},
            std::byte{0x40}},
        staging.span().begin());

    forge_example::require(std::execution::sync_wait(
        forge::accel::mock::copy_to_device(q, bytes, staging)).has_value());
    forge_example::require(std::execution::sync_wait(
        forge::accel::mock::copy_to_host(q, readback, bytes)).has_value());
    forge_example::require(std::ranges::equal(staging.span(), readback.span()));

    forge::accel::mock::device_buffer<int> cached{
        ctx,
        3,
        forge::accel::memory_kind::cached_device};
    forge::accel::mock::host_buffer<int> input{
        ctx,
        3,
        forge::accel::memory_kind::host};
    forge::accel::mock::host_buffer<int> output{ctx, 3};
    std::ranges::copy(std::initializer_list<int>{3, 4, 5}, input.span().begin());

    forge_example::require(std::execution::sync_wait(
        forge::accel::mock::copy_to_device(q, cached, input)).has_value());

    using command = std::execution::completion_signatures<
        std::execution::set_value_t(),
        std::execution::set_error_t(forge::accel::error),
        std::execution::set_stopped_t()>;

    forge::erased_sender<command> invalid_read{
        forge::accel::mock::copy_to_host_typed(q, output, cached)};
    auto error_result = forge::wait_result(std::move(invalid_read));
    forge_example::require(error_result.has_error());
    auto* error = error_result.error_if<forge::accel::error>();
    forge_example::require(error != nullptr);
    forge_example::require(error->kind == forge::accel::error_kind::coherence_required);

    forge_example::require(std::execution::sync_wait(
        forge::accel::mock::flush(q, cached)).has_value());
    forge_example::require(std::execution::sync_wait(
        forge::accel::mock::copy_to_host(q, output, cached)).has_value());
    forge_example::require(std::ranges::equal(input.span(), output.span()));
}
