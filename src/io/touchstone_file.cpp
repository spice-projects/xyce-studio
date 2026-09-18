#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "../core/step_information.h"
#include "../core/util.h"
#include "../core/view.h"
#include "../expression/expression.h"
#include "../expression/expression_manager.h"
#include "touchstone_file.h"
#include "xyce_output_file.h"

namespace
{
    // touchstone format version
    enum class TouchstoneFormat
    {
        V1,
        V2
    };

    // frequency units
    enum class FrequencyUnit
    {
        HZ,
        KHZ,
        MHZ,
        GHZ
    };

    // network parameter types
    enum class ParameterType
    {
        S,
        Y,
        Z
    };

    // data representation formats
    enum class DataFormat
    {
        RI,
        MA,
        DB
    };

    // two-port s-parameter data ordering
    enum class TwoPortOrder
    {
        ORDER_12_21,
        ORDER_21_12
    };

    // parsed touchstone header
    struct TouchstoneHeader
    {
        TouchstoneFormat format = TouchstoneFormat::V1;
        FrequencyUnit freq_unit = FrequencyUnit::HZ;
        ParameterType param_type = ParameterType::S;
        DataFormat data_format = DataFormat::RI;
        double default_r = 50.0;
        std::vector<double> reference_impedances;
        int num_ports = 0;
        TwoPortOrder two_port_order = TwoPortOrder::ORDER_12_21;
        int num_frequencies = 0;
    };

    // tolerance for the log10 ratio variation between consecutive frequency points
    // touchstone files are text-based, so a slightly larger tolerance than binary
    // raw files is used to accommodate rounding from fixed-precision output
    constexpr double LOG_RATIO_TOLERANCE = 1e-7;
    // tolerance for how close the log10 ratio must match an integer step count
    constexpr double SWEEP_RESIDUAL_TOLERANCE = 1e-6;

    // parse the # option line from a touchstone file
    void parse_option_line(const std::string& line, TouchstoneHeader& header) {
        // strip inline comments
        std::string clean = line;
        if (size_t exclamation = clean.find('!'); exclamation != std::string::npos)
            clean = clean.substr(0, exclamation);
        // trim whitespace
        clean = trim(clean);
        // tokenize
        std::vector<std::string> tokens = tokenize_owned(clean);
        // skip if not an option line
        if (tokens.empty() || tokens[0] != "#")
            return;
        // iterate tokens after #
        size_t i = 1;
        while (i < tokens.size()) {
            // normalize token to lowercase
            const std::string lower = to_lower(tokens[i]);
            // check frequency unit
            if (lower == "hz") {
                header.freq_unit = FrequencyUnit::HZ;
                i++;
            }
            else if (lower == "khz") {
                header.freq_unit = FrequencyUnit::KHZ;
                i++;
            }
            else if (lower == "mhz") {
                header.freq_unit = FrequencyUnit::MHZ;
                i++;
            }
            else if (lower == "ghz") {
                header.freq_unit = FrequencyUnit::GHZ;
                i++;
            }
            // check parameter type
            else if (lower == "s") {
                header.param_type = ParameterType::S;
                i++;
            }
            else if (lower == "y") {
                header.param_type = ParameterType::Y;
                i++;
            }
            else if (lower == "z") {
                header.param_type = ParameterType::Z;
                i++;
            }
            // check data format
            else if (lower == "ri") {
                header.data_format = DataFormat::RI;
                i++;
            }
            else if (lower == "ma") {
                header.data_format = DataFormat::MA;
                i++;
            }
            else if (lower == "db") {
                header.data_format = DataFormat::DB;
                i++;
            }
            // check reference impedance
            else if (lower == "r") {
                // check next token exists
                if (i + 1 < tokens.size()) {
                    header.default_r = std::stod(tokens[i + 1]);
                    i += 2;
                }
                else {
                    i++;
                }
            }
            else {
                i++;
            }
        }
    }

    // parse a touchstone v2 keyword line [keyword] value ...
    void parse_keyword_line(const std::string& line, TouchstoneHeader& header) {
        // strip inline comments
        std::string clean = line;
        if (size_t exclamation = clean.find('!'); exclamation != std::string::npos)
            clean = clean.substr(0, exclamation);
        // trim whitespace
        clean = trim(clean);
        // check for bracketed keyword
        if (!clean.starts_with("["))
            return;
        // find closing bracket
        size_t bracket_end = clean.find(']');
        // skip lines without closing bracket
        if (bracket_end == std::string::npos)
            return;
        // extract keyword text between brackets
        std::string keyword = trim(clean.substr(1, bracket_end - 1));
        // normalize keyword to lowercase
        std::string keyword_lower = to_lower(keyword);
        // extract the value portion after the closing bracket
        std::string value_part = trim(clean.substr(bracket_end + 1));
        // extract value tokens by whitespace
        std::vector<std::string> value_tokens = tokenize_owned(value_part);
        // process based on keyword
        if (keyword_lower == "version") {
            header.format = TouchstoneFormat::V2;
        }
        else if (keyword_lower == "number of ports" && !value_tokens.empty()) {
            header.num_ports = std::stoi(value_tokens[0]);
        }
        else if (keyword_lower == "two-port data order" && !value_tokens.empty()) {
            std::string val_lower = to_lower(value_tokens[0]);
            if (val_lower == "12_21")
                header.two_port_order = TwoPortOrder::ORDER_12_21;
            else if (val_lower == "21_12")
                header.two_port_order = TwoPortOrder::ORDER_21_12;
        }
        else if (keyword_lower == "number of frequencies" && !value_tokens.empty()) {
            header.num_frequencies = std::stoi(value_tokens[0]);
        }
        else if (keyword_lower == "reference") {
            // clear existing reference impedances
            header.reference_impedances.clear();
            // loop value tokens
            for (const auto& tok : value_tokens) {
                try {
                    // parse impedance value
                    header.reference_impedances.push_back(std::stod(tok));
                }
                catch (...) {
                }
            }
        }
    }

    // normalize a frequency value to hertz
    double normalize_frequency(double value, FrequencyUnit unit) {
        switch (unit) {
        case FrequencyUnit::HZ:
            return value;
        case FrequencyUnit::KHZ:
            return value * 1e3;
        case FrequencyUnit::MHZ:
            return value * 1e6;
        case FrequencyUnit::GHZ:
            return value * 1e9;
        }
        return value;
    }

    // convert a pair of real values to a complex number based on data format
    std::complex<double> make_complex(double v1, double v2, DataFormat format) {
        switch (format) {
        case DataFormat::RI:
            // real-imaginary: construct directly
            return std::complex<double>(v1, v2);
        case DataFormat::MA:
            // magnitude-angle: polar form, angle in degrees
            return std::polar(v1, v2 * std::numbers::pi / 180.0);
        case DataFormat::DB:
            // decibel-angle: convert dB to linear magnitude, angle in degrees
            return std::polar(std::pow(10.0, v1 / 20.0), v2 * std::numbers::pi / 180.0);
        }
        return std::complex<double>(v1, v2);
    }

    // detect whether the abscissa is linearly or logarithmically spaced
    AbscissaScale detect_abscissa_scale(const std::vector<double>& abscissa) {
        // logarithmic sweeps require at least three points
        if (abscissa.size() < 3)
            return AbscissaScale::LINEAR;
        // initialize the reference log10 ratio
        double reference_ratio = 0.0;
        // loop over consecutive abscissa pairs
        for (size_t i = 1; i < abscissa.size(); ++i) {
            // get previous value
            double previous = abscissa[i - 1];
            // get current value
            double current = abscissa[i];
            // logarithmic sweeps require strictly positive, increasing values
            if (previous <= 0.0 || current <= previous)
                return AbscissaScale::LINEAR;
            // compute the log10 ratio
            double ratio = std::log10(current / previous);
            // record the reference ratio
            if (i == 1) {
                reference_ratio = ratio;
                continue;
            }
            // check the ratio is uniform across the sweep
            if (std::abs(ratio - reference_ratio) > LOG_RATIO_TOLERANCE)
                return AbscissaScale::LINEAR;
        }
        // compute the decade points per interval
        const long decade_steps = std::lround(1.0 / reference_ratio);
        // compute the octave points per interval
        const long octave_steps = std::lround(std::log10(2.0) / reference_ratio);
        // decade residual, degenerate step counts are ineligible
        const double decade_residual = decade_steps >= 2 ? std::abs(reference_ratio - 1.0 / static_cast<double>(decade_steps)) : std::numeric_limits<double>::max();
        // octave residual, degenerate step counts are ineligible
        const double octave_residual = octave_steps >= 2 ? std::abs(reference_ratio - std::log10(2.0) / static_cast<double>(octave_steps)) : std::numeric_limits<double>::max();
        // best candidate residual
        const double best_residual = std::min(decade_residual, octave_residual);
        // check the best candidate is close to an integer step count
        if (best_residual > SWEEP_RESIDUAL_TOLERANCE)
            return AbscissaScale::LINEAR;
        // return the winning candidate
        return decade_residual <= octave_residual ? AbscissaScale::DECADE : AbscissaScale::OCTAVE;
    }

    // infer the number of ports from the .sNp file extension
    int infer_port_count_from_filename(const std::filesystem::path& path) {
        // get the file extension in lowercase
        std::string ext = to_lower(path.extension().string());
        // check for .sNp pattern
        if (ext.size() >= 4 && ext.front() == '.' && ext[1] == 's' && ext.back() == 'p') {
            // extract the number between 's' and 'p'
            std::string num_str = ext.substr(2, ext.size() - 3);
            try {
                return std::stoi(num_str);
            }
            catch (...) {
            }
        }
        // default to zero if parsing fails
        return 0;
    }

    // convert parameter type enum to string
    std::string to_string(ParameterType pt) {
        switch (pt) {
        case ParameterType::S:
            return "S";
        case ParameterType::Y:
            return "Y";
        case ParameterType::Z:
            return "Z";
        }
        return "S";
    }

    // convert data format enum to string
    std::string to_string(DataFormat df) {
        switch (df) {
        case DataFormat::RI:
            return "RI";
        case DataFormat::MA:
            return "MA";
        case DataFormat::DB:
            return "DB";
        }
        return "RI";
    }
} // namespace

std::optional<std::shared_ptr<XyceOutputFile>> touchstone_file_parser(const std::filesystem::path& filename, const StepInformation* step_info) {
    // check if file exists
    if (!std::filesystem::exists(filename))
        return {};
    // record start time
    auto start_time = std::chrono::steady_clock::now();
    // log information
    spdlog::info("Parsing Touchstone file: {}", filename.string());
    // open the file
    std::ifstream file(filename);
    // check file is open
    if (!file.is_open())
        return {};
    // initialize header
    TouchstoneHeader header;
    // attempt to infer port count from filename early (for v1 and v2 without [Number of Ports])
    header.num_ports = infer_port_count_from_filename(filename);
    // state: header mode vs data mode
    bool data_started = false;
    // fallback: collect all numbers when port count is unknown
    std::vector<double> all_numbers;
    // direct processing: frequencies and s-parameter storage
    std::vector<double> frequencies;
    std::vector<std::vector<std::complex<double>>> s_params;
    // buffer for continuation lines (at most nums_per_freq-1 leftover tokens)
    std::vector<double> token_buffer;
    // number of frequency points parsed
    int n_points = 0;
    // process the file line by line
    std::string line;
    while (std::getline(file, line)) {
        // strip inline comments
        std::string clean = line;
        if (size_t exclamation = clean.find('!'); exclamation != std::string::npos)
            clean = clean.substr(0, exclamation);
        // trim whitespace
        clean = trim(clean);
        // skip empty lines
        if (clean.empty())
            continue;
        // header mode: parse option lines and keyword headers
        if (!data_started) {
            // parse option line
            if (clean.starts_with("#")) {
                parse_option_line(clean, header);
                continue;
            }
            // parse keyword headers
            if (clean.starts_with("[")) {
                parse_keyword_line(clean, header);
                std::string lower_clean = to_lower(clean);
                // check for network data section start (v2)
                if (lower_clean.find("[network data]") != std::string::npos) {
                    data_started = true;
                    continue;
                }
                // check for end marker before data
                if (lower_clean.find("[end]") != std::string::npos)
                    break;
                continue;
            }
            // v1: first data line encountered, transition to data mode
            data_started = true;
        }
        // data mode: collect or process numeric tokens
        if (data_started) {
            // skip keyword headers in data section
            if (clean.starts_with("[")) {
                std::string lower_clean = to_lower(clean);
                if (lower_clean.find("[end]") != std::string::npos)
                    break;
                continue;
            }
            // skip option lines in data section
            if (clean.starts_with("#"))
                continue;
            // skip comment-only lines
            if (clean.starts_with("!"))
                continue;
            // tokenize the data line
            std::vector<std::string> tokens = tokenize_owned(clean);
            // check if we know the port count for direct processing
            if (header.num_ports > 0) {
                // initialize data structures on first data encounter
                if (s_params.empty()) {
                    // estimate capacity from declared frequency count
                    size_t estimated_points = header.num_frequencies > 0 ? static_cast<size_t>(header.num_frequencies) : 1024;
                    // reserve memory for frequencies
                    frequencies.reserve(estimated_points);
                    // allocate s-parameter vectors
                    s_params.resize(static_cast<size_t>(header.num_ports) * header.num_ports);
                    for (auto& param : s_params)
                        param.reserve(estimated_points);
                }
                // parse numeric tokens into buffer
                for (const auto& token : tokens) {
                    try {
                        // parse the token as a double
                        token_buffer.push_back(std::stod(token));
                    }
                    catch (...) {
                        // log and skip invalid tokens
                        spdlog::warn("Touchstone parse: skipping non-numeric token '{}'", token);
                    }
                }
                // process complete frequency points from buffer
                const int n = header.num_ports;
                const int nums_per_freq = 1 + n * n * 2;
                while (token_buffer.size() >= static_cast<size_t>(nums_per_freq)) {
                    // parse and normalize the frequency value
                    double freq_raw = token_buffer[0];
                    // convert to hertz
                    double freq_hz = normalize_frequency(freq_raw, header.freq_unit);
                    // append frequency
                    frequencies.push_back(freq_hz);
                    // iterate over s-parameter pairs
                    for (int r = 0; r < n; ++r) {
                        for (int c = 0; c < n; ++c) {
                            // compute source index based on ordering convention
                            int s_index;
                            if (n == 2) {
                                // two-port ordering depends on the declared convention
                                int pair_idx;
                                if (header.two_port_order == TwoPortOrder::ORDER_12_21) {
                                    // 12_21: data is S11, S21, S12, S22
                                    if (r == 0 && c == 0)
                                        pair_idx = 0;
                                    else if (r == 1 && c == 0)
                                        pair_idx = 1;
                                    else if (r == 0 && c == 1)
                                        pair_idx = 2;
                                    else
                                        pair_idx = 3;
                                }
                                else {
                                    // 21_12: data is S11, S12, S21, S22
                                    pair_idx = r * n + c;
                                }
                                s_index = pair_idx;
                            }
                            else {
                                // multi-port: standard row-major ordering
                                s_index = r * n + c;
                            }
                            // compute source position in the buffer
                            size_t src_idx = static_cast<size_t>(s_index) * 2;
                            // parse the value pair
                            double v1 = token_buffer[1 + src_idx];
                            double v2 = token_buffer[1 + src_idx + 1];
                            // convert to complex based on data format
                            std::complex<double> val = make_complex(v1, v2, header.data_format);
                            // append to the appropriate s-parameter vector
                            s_params[static_cast<size_t>(r) * static_cast<size_t>(n) + static_cast<size_t>(c)].push_back(val);
                        }
                    }
                    // remove processed tokens from buffer
                    token_buffer.erase(token_buffer.begin(), token_buffer.begin() + nums_per_freq);
                    // increment frequency point count
                    n_points++;
                }
            }
            else {
                // fallback: collect all numbers for later processing
                for (const auto& token : tokens) {
                    try {
                        // parse the token as a double
                        all_numbers.push_back(std::stod(token));
                    }
                    catch (...) {
                        // log and skip invalid tokens
                        spdlog::warn("Touchstone parse: skipping non-numeric token '{}'", token);
                    }
                }
            }
        }
    }
    // close the file
    file.close();
    // handle fallback: port count was unknown, process collected numbers
    if (!all_numbers.empty() && s_params.empty()) {
        // default to 2 ports if not determined
        if (header.num_ports == 0)
            header.num_ports = 2;
        // get port count
        const int n = header.num_ports;
        // compute expected values per frequency point (freq + n*n*2 for complex params)
        const int nums_per_freq = 1 + n * n * 2;
        // validate data size
        if (all_numbers.size() < static_cast<size_t>(nums_per_freq)) {
            spdlog::warn("Touchstone file has insufficient data for {}-port network", n);
            return {};
        }
        // compute number of frequency points
        n_points = static_cast<int>(all_numbers.size() / nums_per_freq);
        // clamp to declared frequency count if specified
        if (header.num_frequencies > 0 && n_points > header.num_frequencies)
            n_points = header.num_frequencies;
        // initialize frequency vector
        frequencies.reserve(n_points);
        // initialize s-parameter matrix storage (n*n complex vectors, one per s-param)
        s_params.resize(static_cast<size_t>(n) * n);
        // reserve memory for each s-parameter vector
        for (auto& param : s_params)
            param.reserve(n_points);
        // loop frequency points
        for (int i = 0; i < n_points; ++i) {
            // compute offset into the flat data array
            size_t offset = static_cast<size_t>(i) * static_cast<size_t>(nums_per_freq);
            // parse and normalize the frequency value
            double freq_raw = all_numbers[offset];
            // convert to hertz
            double freq_hz = normalize_frequency(freq_raw, header.freq_unit);
            // append frequency
            frequencies.push_back(freq_hz);
            // iterate over s-parameter pairs
            for (int r = 0; r < n; ++r) {
                for (int c = 0; c < n; ++c) {
                    // compute source index based on ordering convention
                    int s_index;
                    if (n == 2) {
                        // two-port ordering depends on the declared convention
                        int pair_idx;
                        if (header.two_port_order == TwoPortOrder::ORDER_12_21) {
                            // 12_21: data is S11, S21, S12, S22
                            if (r == 0 && c == 0)
                                pair_idx = 0;
                            else if (r == 1 && c == 0)
                                pair_idx = 1;
                            else if (r == 0 && c == 1)
                                pair_idx = 2;
                            else
                                pair_idx = 3;
                        }
                        else {
                            // 21_12: data is S11, S12, S21, S22
                            pair_idx = r * n + c;
                        }
                        s_index = pair_idx;
                    }
                    else {
                        // multi-port: standard row-major ordering
                        s_index = r * n + c;
                    }
                    // compute source position in the data array
                    size_t src_idx = offset + 1 + static_cast<size_t>(s_index) * 2;
                    // parse the value pair
                    double v1 = all_numbers[src_idx];
                    double v2 = all_numbers[src_idx + 1];
                    // convert to complex based on data format
                    std::complex<double> val = make_complex(v1, v2, header.data_format);
                    // append to the appropriate s-parameter vector
                    s_params[static_cast<size_t>(r) * static_cast<size_t>(n) + static_cast<size_t>(c)].push_back(val);
                }
            }
        }
    }
    // check we found data
    if (frequencies.empty()) {
        spdlog::warn("Touchstone file has no data points: {}", filename.string());
        return {};
    }
    // default to 2 ports if not determined
    if (header.num_ports == 0)
        header.num_ports = 2;
    // get port count
    const int n = header.num_ports;
    // determine number of steps and points per step
    int n_steps = 1;
    // initialize points per step to total
    int points_per_step = n_points;
    // use provided step information if available
    if (step_info != nullptr) {
        // get step count from provided step information
        n_steps = static_cast<int>(step_info->length());
        // compute points per step if evenly divisible
        if (n_steps > 0 && n_points % n_steps == 0)
            points_per_step = n_points / n_steps;
        else
            points_per_step = n_points;
    }
    // try to infer steps from declared frequency count
    else if (header.num_frequencies > 0 && n_points > header.num_frequencies && n_points % header.num_frequencies == 0) {
        n_steps = n_points / header.num_frequencies;
        points_per_step = header.num_frequencies;
    }
    // detect abscissa scale from first step's frequency values
    std::vector<double> first_step_freqs(frequencies.begin(), frequencies.begin() + points_per_step);
    // detect the abscissa scale
    AbscissaScale abscissa_scale = detect_abscissa_scale(first_step_freqs);
    // get parameter type as string
    std::string param_str = to_string(header.param_type);
    // get data format as string
    std::string fmt_str = to_string(header.data_format);
    // create step slices for expression manager
    std::vector<std::pair<size_t, size_t>> step_slices;
    // reserve space for step slices
    step_slices.reserve(static_cast<size_t>(n_steps));
    // loop steps
    for (int s = 0; s < n_steps; ++s) {
        // compute start and end indices for this step
        size_t start = static_cast<size_t>(s) * static_cast<size_t>(points_per_step);
        size_t end = start + static_cast<size_t>(points_per_step);
        // append slice
        step_slices.push_back({start, end});
    }
    // create step information
    StepInformation final_step_info({}, {}, {});
    // build step information based on source
    if (step_info != nullptr) {
        // extract keys and values from provided step information
        std::vector<std::string> keys = step_info->keys();
        std::vector<std::vector<double>> values = step_info->values();
        // build abscissa ranges from provided step info
        std::vector<std::pair<double, double>> ranges;
        for (size_t s = 0; s < step_info->length(); ++s)
            ranges.push_back({step_info->step_abscissa_left_value(s), step_info->step_abscissa_right_value(s)});
        // create the step information
        final_step_info = StepInformation(std::move(keys), std::move(values), std::move(ranges));
    }
    else {
        // create step information with inferred or single step
        std::vector<std::string> keys;
        // use "step" as key name for multi-step data
        if (n_steps > 1)
            keys = {"step"};
        // build step values
        std::vector<std::vector<double>> values;
        for (int s = 0; s < n_steps; ++s)
            values.push_back({static_cast<double>(s + 1)});
        // build abscissa value ranges for each step
        std::vector<std::pair<double, double>> ranges;
        for (int s = 0; s < n_steps; ++s) {
            // compute start and end indices for this step
            size_t start = static_cast<size_t>(s) * static_cast<size_t>(points_per_step);
            size_t end = start + static_cast<size_t>(points_per_step) - 1;
            // append range
            ranges.push_back({frequencies[start], frequencies[end]});
        }
        // create the step information
        final_step_info = StepInformation(std::move(keys), std::move(values), std::move(ranges));
    }
    // initialize expressions list
    std::vector<AnyExpression> expressions;
    // reserve space for frequency + n*n s-parameters
    expressions.reserve(1 + n * n);
    // create frequency abscissa as the first expression
    expressions.emplace_back(Expression<double>("frequency", std::move(frequencies), step_slices, "Hz"));
    // create s-parameter expressions
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            // build parameter name, e.g. S11, S12, S21, S22
            std::string name = param_str + std::to_string(r + 1) + std::to_string(c + 1);
            // get reference impedance for this port
            std::unordered_map<std::string, std::string> metadata;
            if (!header.reference_impedances.empty()) {
                // use per-port reference impedance if available
                size_t port_idx = static_cast<size_t>(r);
                if (port_idx < header.reference_impedances.size())
                    metadata["reference_impedance"] = std::to_string(header.reference_impedances[port_idx]);
                else
                    metadata["reference_impedance"] = std::to_string(header.default_r);
            }
            else {
                // use default reference impedance
                metadata["reference_impedance"] = std::to_string(header.default_r);
            }
            // create the s-parameter expression
            expressions.emplace_back(Expression<std::complex<double>>(name, std::move(s_params[static_cast<size_t>(r) * static_cast<size_t>(n) + static_cast<size_t>(c)]), step_slices, "", "", {metadata}));
        }
    }
    // create the expression manager
    ExpressionManager expression_manager(expressions, step_slices);
    // build file-level metadata
    std::unordered_map<std::string, std::string> file_metadata;
    // suggest two default plots, one chart per group: the dB magnitudes of the
    // reflection entries (diagonal, the return-loss view) and of the
    // transmission entries (off-diagonal, the insertion-loss view)
    std::vector<std::vector<std::string>> suggested_plots;
    {
        // names of the dB magnitude expressions per group
        std::vector<std::string> reflection_names;
        std::vector<std::string> transmission_names;
        // loop matrix entries in row-major order, e.g. S11, S12, ...
        for (int r = 0; r < n; ++r) {
            for (int c = 0; c < n; ++c) {
                // build parameter name, e.g. S11
                const std::string name = param_str + std::to_string(r + 1) + std::to_string(c + 1);
                // sort the entry into its group
                (r == c ? reflection_names : transmission_names).push_back("db(" + name + ")");
            }
        }
        // append the groups; a 1-port network has no transmission entries
        suggested_plots.push_back(std::move(reflection_names));
        if (!transmission_names.empty())
            suggested_plots.push_back(std::move(transmission_names));
    }
    // store parameter type
    file_metadata["parameter_type"] = param_str;
    // store port count
    file_metadata["num_ports"] = std::to_string(n);
    // store data format
    file_metadata["data_format"] = fmt_str;
    // store frequency unit name
    file_metadata["frequency_unit"] = "Hz";
    // store reference impedance
    file_metadata["reference_impedance"] = header.reference_impedances.empty() ? std::to_string(header.default_r) : std::to_string(header.reference_impedances[0]);
    // create the output file
    auto xyce_file = std::make_shared<XyceOutputFile>(filename, "LIN Analysis", true, std::move(final_step_info), PlotType::AC, abscissa_scale, std::move(expression_manager), nullptr, std::move(suggested_plots), std::move(file_metadata));
    // log information
    spdlog::info("Successfully parsed Touchstone file: {}, ports: {}, points: {}, elapsed time: {}ms", filename.string(), n, n_points, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count());
    // use file
    return xyce_file;
}
