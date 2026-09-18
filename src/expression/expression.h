#pragma once

#include <algorithm>
#include <chrono>
#include <complex>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>

#include "../core/view.h"

template <typename T>
class Expression
{
public:
    Expression() = delete;

    Expression(const Expression&) = delete;

    Expression(Expression&&) noexcept = default;

    Expression(std::string name, std::vector<View<T>>&& steps, std::string unit, std::string source = "", std::vector<std::unordered_map<std::string, std::string>> metadata = {}) :
        m_name(std::move(name)), m_steps(std::move(steps)), m_unit(std::move(unit)), m_source(std::move(source)), m_metadata(std::move(metadata)) {}

    Expression(std::string name, std::vector<T>&& data, std::vector<std::span<const T>>&& steps, std::string unit, std::string source = "", std::vector<std::unordered_map<std::string, std::string>> metadata = {}) :
        m_name(std::move(name)), m_steps(std::move(steps)), m_unit(std::move(unit)), m_source(std::move(source)), m_cached_data(std::move(data)), m_metadata(std::move(metadata)) {}

    Expression(std::string name, std::vector<T>&& data, const std::vector<std::pair<size_t, size_t>>& step_slices, std::string unit, std::string source = "", std::vector<std::unordered_map<std::string, std::string>> metadata = {}) :
        m_name(std::move(name)), m_unit(std::move(unit)), m_source(std::move(source)), m_cached_data(std::move(data)), m_metadata(std::move(metadata)) {
        // steps
        std::vector<std::span<const T>> steps;
        // reserve space for steps
        steps.reserve(step_slices.size());
        // data pointer
        auto ptr = std::get<std::vector<T>>(m_cached_data).data();
        // populate steps from slices
        for (const auto& [start, end] : step_slices)
            steps.emplace_back(ptr + start, end - start);
        // assign steps
        m_steps = std::move(steps);
    }

    Expression(std::string name, View<T>&& view, const std::vector<std::pair<size_t, size_t>>& step_slices, std::string unit, std::string source = "", std::vector<std::unordered_map<std::string, std::string>> metadata = {}) :
        m_name(std::move(name)), m_unit(std::move(unit)), m_source(std::move(source)), m_metadata(std::move(metadata)) {
        // check view does not own data
        if (view.m_data.empty()) {
            // data vector
            std::vector<T> data;
            // reserve space for data
            data.reserve(view.size());
            // copy data from view to internal vector
            for (auto it = view.begin(); it != view.end(); ++it)
                data.push_back(*it);
            // move data
            m_cached_data = std::move(data);
        }
        else {
            // move data from view to internal vector
            m_cached_data = std::move(view.m_data);
        }
        // steps
        std::vector<std::span<const T>> steps;
        // reserve space for steps
        steps.reserve(step_slices.size());
        // data pointer
        auto ptr = std::get<std::vector<T>>(m_cached_data).data();
        // populate steps from slices
        for (const auto& [start, end] : step_slices)
            steps.emplace_back(ptr + start, end - start);
        // assign steps
        m_steps = std::move(steps);
    }

    ~Expression() = default;

    Expression& operator=(const Expression&) = delete;

    Expression& operator=(Expression&&) noexcept = default;

    [[nodiscard]] const std::string& name() const { return m_name; }

    [[nodiscard]] const std::string& unit() const { return m_unit; }

    [[nodiscard]] const std::string& source() const { return m_source; }

    [[nodiscard]] const std::vector<std::unordered_map<std::string, std::string>>& metadata() const { return m_metadata; }

    [[nodiscard]] size_t step_count() const {
        return std::visit([](auto&& v) { return v.size(); }, m_steps);
    }

    const std::span<const T>& step_data(size_t step_index) {
        // check data has been initialized for this expression
        if (std::holds_alternative<std::monostate>(m_cached_data)) {
            // initialize expression
            initialize_expression_data();
        }
        // at this point only span is available
        const auto& steps = std::get<std::vector<std::span<const T>>>(m_steps);
        // return step at index
        return steps.at(step_index);
    }

    std::span<const T> data() {
        // check data has been initialized for this expression
        if (std::holds_alternative<std::monostate>(m_cached_data)) {
            // initialize expression
            initialize_expression_data();
        }
        // return data (create a span over the whole vector)
        return std::span<const T>(std::get<std::vector<T>>(m_cached_data));
    }

    [[nodiscard]] std::vector<std::pair<size_t, size_t>> step_indices() const {
        // result
        std::vector<std::pair<size_t, size_t>> result;
        // lambda
        auto l = [&result](auto& steps) -> void {
            // reserve capacity
            result.reserve(steps.size());
            // offset
            size_t offset = 0;
            // loop steps
            for (auto& step : steps) {
                // end index for step
                size_t end = offset + step.size();
                // append slice
                result.emplace_back(offset, end);
                // update offset
                offset = end;
            }
        };
        // compute step slices
        std::visit(l, m_steps);
        // return slices
        return result;
    }

    void transform(const std::function<T(T)>& f) {
        // check data has been initialized for this expression
        if (std::holds_alternative<std::monostate>(m_cached_data)) {
            // initialize expression
            initialize_expression_data();
        }
        // at this moment vector contains data
        auto& data = std::get<std::vector<T>>(m_cached_data);
        // transform data in-place
        std::ranges::transform(data, data.begin(), f);
    }

private:
    std::string m_name;
    std::variant<std::vector<View<T>>, std::vector<std::span<const T>>> m_steps;
    std::string m_unit;
    std::string m_source;
    std::variant<std::monostate, std::vector<T>> m_cached_data;
    std::vector<std::unordered_map<std::string, std::string>> m_metadata;

    void initialize_expression_data() {
        // record start time
        auto start = std::chrono::steady_clock::now();
        // steps
        const auto& steps = std::get<std::vector<View<T>>>(m_steps);
        // total vector size & step count
        size_t total_size = 0;
        size_t step_count = steps.size();
        // loop steps
        for (const auto& step : steps) {
            // accumulate size
            total_size += step.size();
        }
        // create data & step vectors
        std::vector<T> concatenated;
        std::vector<std::span<const T>> spans;
        // allocate vectors
        concatenated.reserve(total_size);
        // scratch buffer for blocked extraction of strided steps
        std::vector<T> block;
        // loop steps
        for (const View<T>& step : steps) {
            // data pointer and layout
            const T* base = step.data();
            const size_t stride = step.stride();
            const size_t count = step.size();
            // contiguous steps are copied directly
            if (stride == 1) {
                // append step data to buffer
                concatenated.insert(concatenated.end(), base, base + count);
                // next step
                continue;
            }
            // tile size in elements
            constexpr size_t BLOCK = 8192;
            // large strides never share a tile, copy directly instead
            if (stride >= BLOCK) {
                // loop step data, append it to buffer
                for (size_t j = 0; j < count; ++j)
                    concatenated.emplace_back(base[j * stride]);
                // next step
                continue;
            }
            // allocate the scratch buffer on first use
            if (block.empty())
                block.resize(BLOCK);
            // empty steps have no elements
            if (count == 0)
                continue;
            // total source span for this step, clamped to the last valid element:
            // count * stride would sweep a full stride of padding past the last
            // element, reading past the end of the mapped RAW file for the final
            // block (issue #219)
            const size_t span = (count - 1) * stride + 1;
            // loop source tiles, copying each contiguous tile into the scratch buffer
            // then pulling the strided column out of it (cache friendly instead of a strided walk)
            for (size_t offset = 0; offset < span; offset += BLOCK) {
                // tile length
                const size_t length = std::min(BLOCK, span - offset);
                // copy the contiguous tile into the scratch buffer
                std::copy_n(base + offset, length, block.data());
                // first and last step element indexes inside this tile
                const size_t j_first = (offset + stride - 1) / stride;
                const size_t j_last = (offset + length + stride - 1) / stride;
                // append the step elements extracted from the tile
                for (size_t j = j_first; j < j_last; ++j)
                    concatenated.emplace_back(block[j * stride - offset]);
            }
        }
        // data pointer and offset
        const T* pointer = concatenated.data();
        size_t offset = 0;
        // allocate spans
        spans.reserve(steps.size());
        // loop steps
        for (const auto& step : steps) {
            // step size
            const auto length = step.size();
            // update view
            spans.emplace_back(pointer + offset, length);
            // move offset
            offset += length;
        }
        // update cache field (at the end of the processing since this moves the data into the field)
        m_cached_data = std::move(concatenated);
        // update steps with views on the contiguous memory data
        m_steps = std::move(spans);
        // log information
        spdlog::debug("Expression data for '{}' initialized with {} points across {} steps, elapsed time: {} ms", m_name, total_size, step_count, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    }
};

using AnyExpression = std::variant<Expression<double>, Expression<std::complex<double>>>;
