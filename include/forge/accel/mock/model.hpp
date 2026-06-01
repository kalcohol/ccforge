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

#include "context.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace forge::accel::mock {

struct model_descriptor {
    std::vector<model_io_descriptor> inputs;
    std::vector<model_io_descriptor> outputs;
};

namespace __model_detail {

struct __model_state {
    explicit __model_state(model_descriptor descriptor)
        : descriptor(std::move(descriptor))
    {}

    model_descriptor descriptor;
    std::atomic<bool> loaded{true};
};

[[nodiscard]] inline auto __info(const __model_state& state) noexcept
    -> model_io_info {
    return model_io_info{
        .inputs = state.descriptor.inputs.size(),
        .outputs = state.descriptor.outputs.size(),
    };
}

} // namespace __model_detail

class model_session;
class model_bindings;

namespace __model_detail {
void __validate_bindings(const __model_state&, const model_bindings&);
void __fill_outputs(const model_bindings&);
} // namespace __model_detail

class model {
public:
    model() = default;

    explicit model(model_descriptor descriptor)
        : state_(std::make_shared<__model_detail::__model_state>(
              std::move(descriptor)))
    {}

    [[nodiscard]] bool loaded() const noexcept {
        return state_ && state_->loaded.load(std::memory_order_acquire);
    }

    void unload() noexcept {
        if (state_) {
            state_->loaded.store(false, std::memory_order_release);
        }
    }

    [[nodiscard]] auto info() const noexcept -> model_io_info {
        if (!state_) {
            return {};
        }
        return __model_detail::__info(*state_);
    }

    [[nodiscard]] auto input(std::size_t index) const -> model_io_descriptor {
        if (!state_ || index >= state_->descriptor.inputs.size()) {
            throw operation_error{
                error_kind::invalid_binding,
                "forge::accel::mock::model input index is invalid"};
        }
        return state_->descriptor.inputs[index];
    }

    [[nodiscard]] auto output(std::size_t index) const -> model_io_descriptor {
        if (!state_ || index >= state_->descriptor.outputs.size()) {
            throw operation_error{
                error_kind::invalid_binding,
                "forge::accel::mock::model output index is invalid"};
        }
        return state_->descriptor.outputs[index];
    }

    [[nodiscard]] auto open_session(device dev) const -> model_session;

private:
    explicit model(std::shared_ptr<__model_detail::__model_state> state)
        : state_(std::move(state))
    {}

    friend class model_bindings;
    friend class model_session;

    std::shared_ptr<__model_detail::__model_state> state_;
};

class model_bindings {
public:
    explicit model_bindings(const model& mdl)
        : model_(mdl.state_)
    {
        if (model_) {
            inputs_.resize(model_->descriptor.inputs.size());
            outputs_.resize(model_->descriptor.outputs.size());
        }
    }

    void bind_input(std::size_t index, std::span<const std::byte> bytes) {
        if (!model_ || index >= inputs_.size()) {
            throw operation_error{
                error_kind::invalid_binding,
                "forge::accel::mock::model_bindings input index is invalid"};
        }
        inputs_[index] = bytes;
    }

    void bind_output(std::size_t index, std::span<std::byte> bytes) {
        if (!model_ || index >= outputs_.size()) {
            throw operation_error{
                error_kind::invalid_binding,
                "forge::accel::mock::model_bindings output index is invalid"};
        }
        outputs_[index] = bytes;
    }

private:
    friend void __model_detail::__validate_bindings(
        const __model_detail::__model_state&,
        const model_bindings&);
    friend void __model_detail::__fill_outputs(const model_bindings&);
    friend auto execute(model_session&, model_bindings);
    friend auto execute_typed(model_session&, model_bindings);

    std::shared_ptr<__model_detail::__model_state> model_;
    std::vector<std::optional<std::span<const std::byte>>> inputs_;
    std::vector<std::optional<std::span<std::byte>>> outputs_;
};

class model_session {
public:
    model_session() = default;

    [[nodiscard]] auto info() const noexcept -> model_io_info {
        if (!model_) {
            return {};
        }
        return __model_detail::__info(*model_);
    }

    void reset() noexcept {
        session_.reset();
    }

    [[nodiscard]] bool reset_requested() const noexcept {
        return session_.reset_requested();
    }

private:
    model_session(
        std::shared_ptr<__model_detail::__model_state> model,
        device_session session)
        : model_(std::move(model))
        , session_(std::move(session))
    {}

    friend class model;
    friend auto execute(model_session&, model_bindings);
    friend auto execute_typed(model_session&, model_bindings);

    std::shared_ptr<__model_detail::__model_state> model_;
    device_session session_;
};

inline auto model::open_session(device dev) const -> model_session {
    return model_session{state_, dev.open_session()};
}

namespace __model_detail {

inline void __validate_bindings(
    const __model_state& model,
    const model_bindings& bindings) {
    if (!model.loaded.load(std::memory_order_acquire)) {
        throw operation_error{
            error_kind::invalid_context,
            "forge::accel::mock::execute: model is unloaded"};
    }
    if (bindings.inputs_.size() != model.descriptor.inputs.size() ||
        bindings.outputs_.size() != model.descriptor.outputs.size()) {
        throw operation_error{
            error_kind::invalid_binding,
            "forge::accel::mock::execute: bindings do not match model"};
    }
    for (std::size_t i = 0; i < model.descriptor.inputs.size(); ++i) {
        if (!bindings.inputs_[i]) {
            throw operation_error{
                error_kind::invalid_binding,
                "forge::accel::mock::execute: missing input binding"};
        }
        if (bindings.inputs_[i]->size() != model.descriptor.inputs[i].byte_size) {
            throw operation_error{
                error_kind::size_mismatch,
                "forge::accel::mock::execute: input binding size mismatch"};
        }
    }
    for (std::size_t i = 0; i < model.descriptor.outputs.size(); ++i) {
        if (!bindings.outputs_[i]) {
            throw operation_error{
                error_kind::invalid_binding,
                "forge::accel::mock::execute: missing output binding"};
        }
        if (bindings.outputs_[i]->size() != model.descriptor.outputs[i].byte_size) {
            throw operation_error{
                error_kind::size_mismatch,
                "forge::accel::mock::execute: output binding size mismatch"};
        }
    }
}

inline void __fill_outputs(const model_bindings& bindings) {
    std::uint32_t seed = 0;
    for (auto& input : bindings.inputs_) {
        for (std::byte value : *input) {
            seed = (seed + static_cast<unsigned char>(value)) & 0xffu;
        }
    }
    for (std::size_t output_index = 0; output_index < bindings.outputs_.size();
         ++output_index) {
        auto out = *bindings.outputs_[output_index];
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<std::byte>((seed + output_index + i) & 0xffu);
        }
    }
}

} // namespace __model_detail

inline auto execute(model_session& session, model_bindings bindings) {
    auto model = session.model_;
    return submit(
        session.session_,
        [model = std::move(model), bindings = std::move(bindings)]() mutable {
            if (!model) {
                throw operation_error{
                    error_kind::invalid_context,
                    "forge::accel::mock::execute: invalid model session"};
            }
            __model_detail::__validate_bindings(*model, bindings);
            __model_detail::__fill_outputs(bindings);
        });
}

inline auto execute_typed(model_session& session, model_bindings bindings) {
    return __typed_detail::void_sender(execute(session, std::move(bindings)));
}

} // namespace forge::accel::mock
