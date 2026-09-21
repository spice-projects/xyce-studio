#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "../core/step_information.h"
#include "../core/util.h"
#include "../expression/expression.h"
#include "../expression/expression_manager.h"
#include "mapped_file.h"
#include "xyce_fft_file.h"
#include "xyce_output_file.h"

namespace
{
    // regex pattern for the signal header line
    const std::regex RE_SIGNAL_HEADER(R"(^FFT analysis for (.+):$)");
    // regex pattern for the window line
    const std::regex RE_WINDOW_LINE(R"(Window:\s*(\S+),\s*Start Time:\s*([0-9eE+\-.]+),\s*Stop Time:\s*([0-9eE+\-.]+))");
    // regex pattern for the harmonic line
    const std::regex RE_HARMONIC_LINE(R"(First Harmonic:\s*([0-9eE+\-.]+),\s*Start Freq:\s*([0-9eE+\-.]+),\s*Stop Freq:\s*([0-9eE+\-.]+))");
    // regex pattern for the dc component line
    const std::regex RE_DC_LINE(R"(DC component\s+(.*)\s+Mag=\s*([0-9eE+\-.]+)\s+Phase=\s*([0-9eE+\-.]+))");
    // regex pattern for the numeric fft suffix in file names
    const std::regex RE_FFT_INDEX(R"(fft(\d+)$)");

    // single step of fft data for a signal
    struct FftStep
    {
        std::shared_ptr<std::unordered_map<std::string, std::string>> metadata;
        double dc_magnitude = 0.0;
        double dc_phase = 0.0;
        std::shared_ptr<std::vector<double>> magnitude;
        std::shared_ptr<std::vector<double>> phase;
    };

    // accumulated signals grouped by their abscissa configuration
    struct AbscissaEntry
    {
        std::shared_ptr<std::vector<double>> frequency;
        std::unordered_map<std::string, std::vector<FftStep>> signals;
    };

    // identifies a distinct abscissa configuration
    struct AbscissaKey
    {
        double first_harmonic;
        double start_freq;
        double stop_freq;
        std::string window;
        bool normalized;

        bool operator<(const AbscissaKey& other) const {
            // compare the rounded frequency values
            if (first_harmonic != other.first_harmonic)
                return first_harmonic < other.first_harmonic;
            // compare the start frequency values
            if (start_freq != other.start_freq)
                return start_freq < other.start_freq;
            // compare the stop frequency values
            if (stop_freq != other.stop_freq)
                return stop_freq < other.stop_freq;
            // compare the window names
            if (window != other.window)
                return window < other.window;
            // compare the normalized flag
            return normalized < other.normalized;
        }
    };

    std::vector<std::string> get_tokens(const std::string& line) {
        // initialize tokens
        std::vector<std::string> tokens;
        // initialize start position
        size_t start = 0;
        while (start < line.size()) {
            // skip leading whitespace
            while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])))
                start++;
            // check we reached the end
            if (start >= line.size())
                break;
            // find the end of the token
            size_t end = start;
            while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end])))
                end++;
            // append the token
            tokens.push_back(line.substr(start, end - start));
            // advance start
            start = end;
        }
        // return tokens
        return tokens;
    }

    double round_to(const double value, const int digits) {
        // compute the scaling factor
        const double scale = std::pow(10.0, digits);
        // round to the requested number of decimal places
        return std::round(value * scale) / scale;
    }

    bool wildcard_match(const std::string& pattern, const std::string& text) {
        // position in the pattern and text
        size_t pattern_pos = 0;
        size_t text_pos = 0;
        // position of the last star and the text position it consumed
        size_t star_pos = std::string::npos;
        size_t star_text = 0;
        while (text_pos < text.size()) {
            // check the pattern character matches the text character
            if (pattern_pos < pattern.size() && (pattern[pattern_pos] == '?' || pattern[pattern_pos] == text[text_pos])) {
                // advance both positions
                pattern_pos++;
                text_pos++;
            }
            else if (pattern_pos < pattern.size() && pattern[pattern_pos] == '*') {
                // record the star position for backtracking
                star_pos = pattern_pos++;
                star_text = text_pos;
            }
            else if (star_pos != std::string::npos) {
                // backtrack to the last star and consume one more character
                pattern_pos = star_pos + 1;
                text_pos = ++star_text;
            }
            else {
                // no match possible
                return false;
            }
        }
        // consume any trailing stars
        while (pattern_pos < pattern.size() && pattern[pattern_pos] == '*')
            pattern_pos++;
        // the pattern is exhausted
        return pattern_pos == pattern.size();
    }

    std::vector<std::filesystem::path> glob_matching_files(const std::filesystem::path& pattern) {
        // result
        std::vector<std::filesystem::path> matches;
        // convert the pattern to a string
        const std::string pattern_str = pattern.string();
        // find the last directory separator
        const size_t separator = pattern_str.find_last_of("/\\");
        // directory part of the pattern
        const std::string directory = separator == std::string::npos ? "." : pattern_str.substr(0, separator);
        // filename part of the pattern
        const std::string filename_pattern = separator == std::string::npos ? pattern_str : pattern_str.substr(separator + 1);
        // iterate the directory entries
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
            // only regular files match
            if (!entry.is_regular_file())
                continue;
            // compare the file name against the pattern
            if (wildcard_match(filename_pattern, entry.path().filename().string()))
                matches.push_back(entry.path());
        }
        // return matches
        return matches;
    }

    int get_fft_index(const std::filesystem::path& filepath) {
        // convert the path to a string
        const std::string path_str = filepath.string();
        // search for the numeric suffix
        std::smatch match;
        if (std::regex_search(path_str.begin(), path_str.end(), match, RE_FFT_INDEX))
            return std::stoi(match[1].str());
        // no numeric suffix
        return -1;
    }

    bool is_valid_utf8(const std::string& text) {
        // iterate over the bytes
        size_t i = 0;
        while (i < text.size()) {
            // get the current byte
            const unsigned char c = static_cast<unsigned char>(text[i]);
            // single byte character
            if (c < 0x80) {
                i += 1;
            }
            else if ((c & 0xE0) == 0xC0) {
                // two byte sequence, validate the continuation byte
                if (i + 1 >= text.size() || (static_cast<unsigned char>(text[i + 1]) & 0xC0) != 0x80)
                    return false;
                i += 2;
            }
            else if ((c & 0xF0) == 0xE0) {
                // three byte sequence, validate the continuation bytes
                if (i + 2 >= text.size() || (static_cast<unsigned char>(text[i + 1]) & 0xC0) != 0x80 || (static_cast<unsigned char>(text[i + 2]) & 0xC0) != 0x80)
                    return false;
                i += 3;
            }
            else if ((c & 0xF8) == 0xF0) {
                // four byte sequence, validate the continuation bytes
                if (i + 3 >= text.size() || (static_cast<unsigned char>(text[i + 1]) & 0xC0) != 0x80 || (static_cast<unsigned char>(text[i + 2]) & 0xC0) != 0x80 || (static_cast<unsigned char>(text[i + 3]) & 0xC0) != 0x80)
                    return false;
                i += 4;
            }
            else {
                // invalid leading byte
                return false;
            }
        }
        // valid utf8
        return true;
    }

    std::string strip_brace_ends(const std::string& name) {
        // start and end positions
        size_t start = 0;
        size_t end = name.size();
        // advance past leading braces
        while (start < end && (name[start] == '{' || name[start] == '}'))
            start++;
        // retreat past trailing braces
        while (end > start && (name[end - 1] == '{' || name[end - 1] == '}'))
            end--;
        // return the stripped substring
        return name.substr(start, end - start);
    }

    bool parse_fft_file(const std::filesystem::path& filepath, std::map<AbscissaKey, AbscissaEntry>& signals) {
        // check the file is not empty
        std::error_code ec;
        if (std::filesystem::is_regular_file(filepath) && std::filesystem::file_size(filepath, ec) == 0) {
            // empty files contain no signals
            spdlog::warn("Xyce FFT file is empty: {}", filepath.string());
            // exit
            return true;
        }
        // create the mapped file
        auto mapped_file = std::make_unique<MappedFile>(filepath);
        if (!mapped_file->is_valid())
            return false;
        // get the data pointer and length
        const char* data = mapped_file->data();
        const size_t length = mapped_file->size();
        if (!data || length == 0)
            return false;
        // current scan position
        size_t pos = 0;
        // line number for logging
        int line_number = -1;
        // expected data point index
        size_t expected_index = 0;
        // header section data
        std::string signal_name;
        std::string window;
        double first_harmonic = 0.0;
        double start_freq = 0.0;
        double stop_freq = 0.0;
        bool normalized = false;
        double dc_magnitude = 0.0;
        double dc_phase = 0.0;
        // per block metadata
        auto metadata = std::make_shared<std::unordered_map<std::string, std::string>>();
        // per block data lists
        auto frequency_list = std::make_shared<std::vector<double>>();
        auto magnitude_list = std::make_shared<std::vector<double>>();
        auto phase_list = std::make_shared<std::vector<double>>();
        // flags tracking the found header lines
        bool found_signal = false;
        bool found_window = false;
        bool found_harmonic = false;
        bool found_dc = false;
        try {
            // scan line by line until the end of the file
            while (pos < length) {
                // increment the line number
                line_number++;
                // find the next newline
                const char* newline = static_cast<const char*>(std::memchr(data + pos, '\n', length - pos));
                // check there is no trailing newline, the partial line is skipped
                if (newline == nullptr)
                    break;
                // extract the line content
                std::string line(data + pos, newline - (data + pos));
                // advance the position past the newline
                pos = static_cast<size_t>(newline - data) + 1;
                // validate the utf8 encoding
                if (!is_valid_utf8(line))
                    return false;
                // trim whitespace
                line = trim(line);
                // check we are in the data point section
                if (expected_index > 0) {
                    // check the end of the data points (empty line)
                    if (line.empty()) {
                        // reset the expected index
                        expected_index = 0;
                        // next line
                        continue;
                    }
                    // split the line into columns
                    const std::vector<std::string> columns = get_tokens(line);
                    // validate the number of columns
                    if (columns.size() != 4) {
                        // log the error
                        spdlog::error("invalid Xyce FFT file: unexpected number of columns ({}) in data point line '{}'", columns.size(), line);
                        // exit
                        return false;
                    }
                    // parse the data point index
                    const size_t index = static_cast<size_t>(std::stoull(columns[0]));
                    // validate the index is sequential
                    if (index != expected_index) {
                        // log the error
                        spdlog::error("invalid Xyce FFT file: unexpected data point index {} (expected {}) in '{}'", index, expected_index, filepath.string());
                        // exit
                        return false;
                    }
                    // append the data points
                    frequency_list->push_back(std::stod(columns[1]));
                    magnitude_list->push_back(std::stod(columns[2]));
                    phase_list->push_back(std::stod(columns[3]));
                    // increment the expected index
                    expected_index++;
                    // next line
                    continue;
                }
                // log the line
                spdlog::debug(">> {}", line);
                // check the signal header line
                if (!found_signal) {
                    std::smatch match;
                    if (std::regex_match(line.cbegin(), line.cend(), match, RE_SIGNAL_HEADER)) {
                        // capture the signal name
                        signal_name = match[1].str();
                        // trim whitespace
                        signal_name = trim(signal_name);
                        // update flag
                        found_signal = true;
                        // reset the metadata for the new block
                        metadata = std::make_shared<std::unordered_map<std::string, std::string>>();
                        // next line
                        continue;
                    }
                }
                // check the window line
                if (!found_window) {
                    std::smatch match;
                    if (std::regex_search(line.cbegin(), line.cend(), match, RE_WINDOW_LINE)) {
                        // capture the window name
                        window = match[1].str();
                        found_window = true;
                        // next line
                        continue;
                    }
                }
                // check the harmonic line
                if (!found_harmonic) {
                    std::smatch match;
                    if (std::regex_search(line.cbegin(), line.cend(), match, RE_HARMONIC_LINE)) {
                        // parse the harmonic values
                        first_harmonic = std::stod(match[1].str());
                        start_freq = std::stod(match[2].str());
                        stop_freq = std::stod(match[3].str());
                        found_harmonic = true;
                        // next line
                        continue;
                    }
                }
                // check the dc component line
                if (!found_dc) {
                    std::smatch match;
                    if (std::regex_search(line.cbegin(), line.cend(), match, RE_DC_LINE)) {
                        // detect the normalized flag
                        normalized = match[1].str() == "Norm.";
                        // parse the dc values
                        dc_magnitude = std::stod(match[2].str());
                        dc_phase = std::stod(match[3].str());
                        found_dc = true;
                        // next line
                        continue;
                    }
                }
                // check the index header line
                if (line.starts_with("Index")) {
                    // validate the header lines were found
                    if (!(found_signal && found_window && found_harmonic && found_dc)) {
                        // log the error
                        spdlog::error("invalid Xyce FFT file: missing header lines before data points in '{}'", filepath.string());
                        // exit
                        return false;
                    }
                    // reset the header flags
                    found_signal = false;
                    found_window = false;
                    found_harmonic = false;
                    found_dc = false;
                    // initialize the expected index for the data points
                    expected_index = 1;
                    // reset the data lists
                    frequency_list = std::make_shared<std::vector<double>>();
                    magnitude_list = std::make_shared<std::vector<double>>();
                    phase_list = std::make_shared<std::vector<double>>();
                    // generate the abscissa key
                    const AbscissaKey key{round_to(first_harmonic, 9), round_to(start_freq, 6), round_to(stop_freq, 6), window, normalized};
                    // lookup the abscissa entry
                    auto entry_it = signals.find(key);
                    if (entry_it == signals.end()) {
                        // create the abscissa entry storing the current frequency list
                        AbscissaEntry entry;
                        entry.frequency = frequency_list;
                        entry_it = signals.emplace(key, std::move(entry)).first;
                    }
                    // lookup the signal steps list
                    auto& signal_steps = entry_it->second.signals[signal_name];
                    // append the current step data
                    signal_steps.push_back(FftStep{metadata, dc_magnitude, dc_phase, magnitude_list, phase_list});
                    // log the line
                    spdlog::debug(">> ...");
                    // next line
                    continue;
                }
                // check the thd line
                if (line.starts_with("THD")) {
                    // append the metadata value
                    (*metadata)["THD"] = trim(line.substr(5));
                    // next line
                    continue;
                }
                // check the sndr line
                if (line.starts_with("SNDR")) {
                    // append the metadata value
                    (*metadata)["SNDR"] = trim(line.substr(6));
                    // next line
                    continue;
                }
                // check the enob line
                if (line.starts_with("ENOB")) {
                    // append the metadata value
                    (*metadata)["ENOB"] = trim(line.substr(6));
                    // next line
                    continue;
                }
                // check the snr line
                if (line.starts_with("SNR")) {
                    // append the metadata value
                    (*metadata)["SNR"] = trim(line.substr(5));
                    // next line
                    continue;
                }
                // check the sfdr line
                if (line.starts_with("SFDR")) {
                    // append the metadata value
                    (*metadata)["SFDR"] = trim(line.substr(6));
                    // next line
                    continue;
                }
                // log a warning for any unexpected non-empty line
                if (!line.empty())
                    spdlog::warn("unexpected line in Xyce FFT file '{}' at line {}: '{}'", filepath.string(), line_number, line);
            }
        }
        catch (const std::exception&) {
            // log the error
            spdlog::error("Error processing Xyce FFT file: {}", filepath.string());
            // exit
            return false;
        }
        // parse successful
        return true;
    }

    std::optional<std::vector<std::shared_ptr<XyceOutputFile>>> build_output_files(const std::vector<std::filesystem::path>& matching_files, const StepInformation* step_information, ExpressionManager* expression_manager, const std::map<AbscissaKey, AbscissaEntry>& signals) {
        // step count driving the slices: the analysis step information when
        // provided, otherwise the files' own STEP markers (all signals of an
        // abscissa carry the same step count)
        const size_t step_count = step_information != nullptr ? step_information->length() : (signals.empty() || signals.begin()->second.signals.empty() ? 0 : signals.begin()->second.signals.begin()->second.size());
        // result
        std::vector<std::shared_ptr<XyceOutputFile>> output_files;
        // reserve capacity
        output_files.reserve(signals.size());
        // process each abscissa entry
        for (const auto& [key, entry] : signals) {
            // build the abscissa data, starting at the dc frequency
            std::vector<double> abscissa_data;
            abscissa_data.reserve(entry.frequency->size() + 1);
            abscissa_data.push_back(0.0);
            abscissa_data.insert(abscissa_data.end(), entry.frequency->begin(), entry.frequency->end());
            // step slices for the concatenated data (one slice per step)
            std::vector<std::pair<size_t, size_t>> step_slices;
            step_slices.reserve(step_count);
            for (size_t idx = 0; idx < step_count; ++idx)
                step_slices.emplace_back(idx * abscissa_data.size(), (idx + 1) * abscissa_data.size());
            // abscissa data repeated for every step
            std::vector<double> abscissa_values;
            abscissa_values.reserve(step_count * abscissa_data.size());
            for (size_t idx = 0; idx < step_count; ++idx)
                abscissa_values.insert(abscissa_values.end(), abscissa_data.begin(), abscissa_data.end());
            // initialize the expressions with the abscissa expression
            std::vector<AnyExpression> expressions;
            expressions.emplace_back(Expression<double>("frequency", std::move(abscissa_values), step_slices, "Hz", "FFT"));
            // suggested plots
            std::vector<std::vector<std::string>> suggested_plots;
            // process each signal in this abscissa
            for (const auto& [signal_name, steps] : entry.signals) {
                // validate the step count matches the resolved step count
                if (steps.size() != step_count) {
                    // log the error
                    spdlog::error("invalid Xyce FFT file: inconsistent step count for signal '{}' in '{}': expected {}, found {}", signal_name, matching_files[0].string(), step_count, steps.size());
                    // exit
                    return std::nullopt;
                }
                // strip the braces from the signal name
                const std::string clean_name = strip_brace_ends(signal_name);
                // infer the magnitude unit from the raw file expression manager
                const std::string magnitude_unit = expression_manager != nullptr ? expression_manager->infer_unit(clean_name) : "";
                // concatenated magnitude and phase data
                std::vector<double> magnitude_data;
                std::vector<double> phase_data;
                // per step metadata
                std::vector<std::unordered_map<std::string, std::string>> metadata_steps;
                // reserve capacity
                magnitude_data.reserve(steps.size() * abscissa_data.size());
                phase_data.reserve(steps.size() * abscissa_data.size());
                metadata_steps.reserve(steps.size());
                // process the steps
                for (const auto& step : steps) {
                    // append the dc component followed by the magnitude data
                    magnitude_data.push_back(step.dc_magnitude);
                    magnitude_data.insert(magnitude_data.end(), step.magnitude->begin(), step.magnitude->end());
                    // append the dc component followed by the phase data
                    phase_data.push_back(step.dc_phase);
                    phase_data.insert(phase_data.end(), step.phase->begin(), step.phase->end());
                    // append the step metadata
                    metadata_steps.push_back(*step.metadata);
                }
                // append to suggested plots (a maximum of three charts are suggested for plotting)
                if (suggested_plots.size() < 3)
                    suggested_plots.push_back({"FFT(" + clean_name + ")", "FFT(phase(" + clean_name + "))"});
                // create the magnitude and phase expressions
                expressions.emplace_back(Expression<double>("FFT(" + clean_name + ")", std::move(magnitude_data), step_slices, magnitude_unit, "FFT", metadata_steps));
                expressions.emplace_back(Expression<double>("FFT(phase(" + clean_name + "))", std::move(phase_data), step_slices, "°", "FFT", metadata_steps));
            }
            // abscissa value ranges (dc to the last frequency) for every step
            std::vector<std::pair<double, double>> value_ranges;
            value_ranges.reserve(step_count);
            for (size_t idx = 0; idx < step_count; ++idx)
                value_ranges.emplace_back(abscissa_data.front(), abscissa_data.back());
            // create the step information; the analysis step information is
            // carried over when provided, otherwise the files define a
            // step-less single-block dataset
            StepInformation fft_step_information(step_information != nullptr ? step_information->keys() : std::vector<std::string>{}, step_information != nullptr ? step_information->values() : std::vector<std::vector<double>>{}, std::move(value_ranges));
            // create the expression manager
            ExpressionManager fft_expression_manager(expressions, step_slices);
            // file metadata
            std::unordered_map<std::string, std::string> metadata{
                {"Window", key.window}, {"Normalized", key.normalized ? "true" : "false"}, {"First Harmonic", std::to_string(key.first_harmonic)}, {"start_freq", std::to_string(key.start_freq)}, {"stop_freq", std::to_string(key.stop_freq)},
            };
            // build title from the abscissa configuration (integer Hz with no trailing zeros)
            std::string fft_title = "FFT: " + key.window + ", " + std::to_string(static_cast<long long>(key.start_freq)) + "–" + std::to_string(static_cast<long long>(key.stop_freq)) + " Hz," + "fh=" + std::to_string(static_cast<long long>(key.first_harmonic)) + "," + (key.normalized ? " Norm" : " Pure");
            // create the output file
            output_files.push_back(std::make_shared<XyceOutputFile>(matching_files[0], std::move(fft_title), false, std::move(fft_step_information), PlotType::FFT, AbscissaScale::LINEAR, std::move(fft_expression_manager), nullptr, suggested_plots, std::move(metadata)));
        }
        // return the output files
        return output_files;
    }
} // namespace

std::optional<std::vector<std::shared_ptr<XyceOutputFile>>> xyce_fft_file_parser(const std::filesystem::path& file_pattern, const StepInformation* step_information, ExpressionManager* expression_manager) {
    // record the start time
    auto start_time = std::chrono::steady_clock::now();
    // find all files matching the pattern
    std::vector<std::filesystem::path> matching_files = glob_matching_files(file_pattern);
    // check no files matched
    if (matching_files.empty()) {
        // log the error
        spdlog::error("No Xyce FFT files found matching pattern: {}", file_pattern.string());
        return std::nullopt;
    }
    // sort the files by their fft numeric suffix
    std::stable_sort(matching_files.begin(), matching_files.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) { return get_fft_index(lhs) < get_fft_index(rhs); });
    // accumulated signals grouped by abscissa configuration
    std::map<AbscissaKey, AbscissaEntry> signals;
    // process each matching file
    for (const auto& filepath : matching_files) {
        // log the information
        spdlog::info("Loading Xyce FFT file: {}", filepath.string());
        // parse the file into the accumulator
        if (!parse_fft_file(filepath, signals))
            return std::nullopt;
    }
    // build the output files
    auto output_files = build_output_files(matching_files, step_information, expression_manager, signals); // log the information
    if (output_files)
        spdlog::info("Successfully parsed Xyce FFT files: {}, elapsed time: {}ms", matching_files.size(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count());
    // return the output files
    return output_files;
}
