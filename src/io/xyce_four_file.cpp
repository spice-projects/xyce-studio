#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "../core/step_information.h"
#include "../core/util.h"
#include "../expression/expression.h"
#include "../expression/expression_manager.h"
#include "mapped_file.h"
#include "xyce_four_file.h"
#include "xyce_output_file.h"

namespace
{
    // regex pattern for the signal header line
    const std::regex RE_SIGNAL_HEADER(R"(^Fourier analysis for (.+):$)");
    // regex pattern for the analysis metadata line; the THD field may be a
    // non-finite token such as `nan`, so it is captured as anything up to the
    // Gridsize separator rather than a numeric character class
    const std::regex RE_METADATA_LINE(R"(No\. Harmonics:\s*(\d+),\s*THD:\s*([^,]+),\s*Gridsize:\s*(\d+),\s*Interpolation Type:\s*(.+)$)");
    // regex pattern for the numeric step suffix in file names
    const std::regex RE_FOUR_INDEX(R"(four(\d+)$)");

    // single step of Fourier data for a signal
    struct FourSignalStep
    {
        std::shared_ptr<std::unordered_map<std::string, std::string>> metadata;
        std::shared_ptr<std::vector<std::complex<double>>> spectrum;
    };

    // accumulated signals grouped by their fundamental frequency
    struct FourEntry
    {
        std::shared_ptr<std::vector<double>> frequency;
        std::vector<std::string> order;
        std::unordered_map<std::string, std::vector<FourSignalStep>> signals;
    };

    // identifies a distinct fundamental frequency configuration
    struct FourKey
    {
        double fundamental;
        size_t harmonics;

        bool operator<(const FourKey& other) const {
            // compare the fundamental frequencies
            if (fundamental != other.fundamental)
                return fundamental < other.fundamental;
            // compare the harmonic counts
            return harmonics < other.harmonics;
        }
    };

    // round a value to the requested number of decimal places
    double round_to(const double value, const int digits) {
        // compute the scaling factor
        const double scale = std::pow(10.0, digits);
        // round to the requested number of decimal places
        return std::round(value * scale) / scale;
    }

    // glob-style match supporting the star and question mark wildcards
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

    // list the regular files in the pattern's directory matching the file name glob
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

    // extract the numeric step suffix from a file name
    int get_four_index(const std::filesystem::path& filepath) {
        // convert the path to a string
        const std::string path_str = filepath.string();
        // search for the numeric suffix
        std::smatch match;
        if (std::regex_search(path_str.begin(), path_str.end(), match, RE_FOUR_INDEX))
            return std::stoi(match[1].str());
        // no numeric suffix
        return -1;
    }

    // validate the utf8 encoding of a line
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

    // remove the wrapping braces from an expression-style signal name
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

    // append a finalized signal block to its fundamental frequency entry
    void append_block(std::map<FourKey, FourEntry>& signals, const std::string& signal_name, const size_t harmonics, const std::shared_ptr<std::unordered_map<std::string, std::string>>& metadata, const std::shared_ptr<std::vector<double>>& frequency, const std::shared_ptr<std::vector<std::complex<double>>>& spectrum) {
        // the fundamental is the frequency of the first harmonic
        const double fundamental = frequency->size() > 1 ? round_to((*frequency)[1], 6) : 0.0;
        // build the entry key from the fundamental and the harmonic count
        const FourKey key{fundamental, harmonics};
        // lookup the fundamental entry
        auto entry_it = signals.find(key);
        if (entry_it == signals.end()) {
            // create the entry storing the shared harmonic frequency axis
            FourEntry entry;
            entry.frequency = frequency;
            entry_it = signals.emplace(key, std::move(entry)).first;
        }
        // keep the signal names in their first-seen order
        auto& entry = entry_it->second;
        if (std::ranges::find(entry.order, signal_name) == entry.order.end())
            entry.order.push_back(signal_name);
        // append the current step spectrum
        entry.signals[signal_name].push_back(FourSignalStep{metadata, spectrum});
    }

    // parse one .FOUR file into the accumulator
    bool parse_four_file(const std::filesystem::path& filepath, std::map<FourKey, FourEntry>& signals) {
        // check the file is empty
        std::error_code ec;
        if (std::filesystem::file_size(filepath, ec) == 0) {
            // empty files contain no signals
            spdlog::warn("Xyce FOUR file is empty: {}", filepath.string());
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
        // current scan position
        size_t pos = 0;
        // line number for logging
        int line_number = -1;
        // current signal block state
        std::string signal_name;
        size_t harmonics = 0;
        // flags tracking the found header lines
        bool found_signal = false;
        bool found_metadata = false;
        // data section state
        bool in_data = false;
        size_t expected_index = 0;
        // per block metadata and data lists
        auto metadata = std::make_shared<std::unordered_map<std::string, std::string>>();
        auto frequency_list = std::make_shared<std::vector<double>>();
        auto spectrum_list = std::make_shared<std::vector<std::complex<double>>>();
        // append the completed block to the accumulator
        auto finalize_block = [&]() {
            // only a fully parsed block is appended
            if (found_signal && found_metadata)
                append_block(signals, signal_name, harmonics, metadata, frequency_list, spectrum_list);
        };
        try {
            // scan line by line until the end of the file
            while (pos < length) {
                // increment the line number
                line_number++;
                // find the next newline
                const char* newline = static_cast<const char*>(std::memchr(data + pos, '\n', length - pos));
                // the last line may lack a trailing newline
                const size_t line_end = newline == nullptr ? length : static_cast<size_t>(newline - data);
                // extract the line content
                std::string line(data + pos, line_end - pos);
                // advance the position past the newline
                pos = newline == nullptr ? length : line_end + 1;
                // validate the utf8 encoding
                if (!is_valid_utf8(line))
                    return false;
                // trim whitespace
                line = trim(line);
                // check we are in the data point section
                if (in_data) {
                    // check the end of the data points (empty line)
                    if (line.empty()) {
                        // append the completed block
                        finalize_block();
                        // reset the flags for the next block
                        found_signal = false;
                        found_metadata = false;
                        in_data = false;
                        // next line
                        continue;
                    }
                    // split the line into columns
                    const std::vector<std::string> columns = tokenize_owned(line);
                    // validate the number of columns
                    if (columns.size() != 6) {
                        // log the error
                        spdlog::error("invalid Xyce FOUR file: unexpected number of columns ({}) in harmonic line '{}'", columns.size(), line);
                        // exit
                        return false;
                    }
                    // parse the harmonic index
                    const size_t index = static_cast<size_t>(std::stoull(columns[0]));
                    // validate the index is sequential
                    if (index != expected_index) {
                        // log the error
                        spdlog::error("invalid Xyce FOUR file: unexpected harmonic index {} (expected {}) in '{}'", index, expected_index, filepath.string());
                        // exit
                        return false;
                    }
                    // parse the frequency of the harmonic
                    frequency_list->push_back(std::stod(columns[1]));
                    // convert the magnitude and phase (degrees) into a complex value
                    const double magnitude = std::stod(columns[2]);
                    const double phase_radians = std::stod(columns[3]) * std::numbers::pi / 180.0;
                    // compute the complex value manually so a negative magnitude is preserved
                    const double real = magnitude * std::cos(phase_radians);
                    const double imag = magnitude * std::sin(phase_radians);
                    // append the complex spectrum value
                    spectrum_list->push_back(std::complex<double>(real, imag));
                    // increment the expected index
                    expected_index++;
                    // next line
                    continue;
                }
                // check the signal header line
                if (!found_signal) {
                    std::smatch match;
                    if (std::regex_match(line.cbegin(), line.cend(), match, RE_SIGNAL_HEADER)) {
                        // capture the signal name
                        signal_name = trim(match[1].str());
                        // reset the per block state
                        metadata = std::make_shared<std::unordered_map<std::string, std::string>>();
                        frequency_list = std::make_shared<std::vector<double>>();
                        spectrum_list = std::make_shared<std::vector<std::complex<double>>>();
                        harmonics = 0;
                        found_signal = true;
                        // next line
                        continue;
                    }
                }
                // check the metadata line
                if (!found_metadata) {
                    std::smatch match;
                    if (std::regex_search(line.cbegin(), line.cend(), match, RE_METADATA_LINE)) {
                        // parse the harmonic count
                        harmonics = static_cast<size_t>(std::stoull(match[1].str()));
                        // store the analysis metadata
                        (*metadata)["No. Harmonics"] = match[1].str();
                        (*metadata)["THD"] = trim(match[2].str());
                        (*metadata)["Gridsize"] = match[3].str();
                        (*metadata)["Interpolation Type"] = trim(match[4].str());
                        found_metadata = true;
                        // next line
                        continue;
                    }
                }
                // check the column header line starting the data section
                if (line.starts_with("Harmonic")) {
                    // validate the header lines were found
                    if (!(found_signal && found_metadata)) {
                        // log the error
                        spdlog::error("invalid Xyce FOUR file: missing header lines before harmonic data in '{}'", filepath.string());
                        // exit
                        return false;
                    }
                    // initialize the expected harmonic index for the data rows
                    expected_index = 0;
                    // enter the data section
                    in_data = true;
                    // next line
                    continue;
                }
                // log a warning for any unexpected non-empty line
                if (!line.empty())
                    spdlog::warn("unexpected line in Xyce FOUR file '{}' at line {}: '{}'", filepath.string(), line_number, line);
            }
        }
        catch (const std::exception&) {
            // log the error
            spdlog::error("Error processing Xyce FOUR file: {}", filepath.string());
            // exit
            return false;
        }
        // append a trailing block that reached the end of the file
        finalize_block();
        // parse successful
        return true;
    }

    // assemble the output files from the accumulated signals
    std::optional<std::vector<std::shared_ptr<XyceOutputFile>>> build_output_files(const std::vector<std::filesystem::path>& matching_files, const StepInformation* step_information, ExpressionManager* expression_manager, const std::map<FourKey, FourEntry>& signals) {
        // step count driving the slices: the analysis step information when
        // provided, otherwise the files' own step markers (all signals of a
        // fundamental carry the same step count)
        const size_t step_count = step_information != nullptr ? step_information->length() : (signals.empty() ? 0 : signals.begin()->second.signals.at(signals.begin()->second.order.front()).size());
        // result
        std::vector<std::shared_ptr<XyceOutputFile>> output_files;
        // reserve capacity
        output_files.reserve(signals.size());
        // process each fundamental entry
        for (const auto& [key, entry] : signals) {
            // the harmonic frequency axis covering the dc component and the harmonics
            const std::vector<double>& abscissa_data = *entry.frequency;
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
            expressions.emplace_back(Expression<double>("frequency", std::move(abscissa_values), step_slices, "Hz", "FOUR"));
            // process each signal in its first-seen order
            for (const auto& signal_name : entry.order) {
                // locate the per step spectra of this signal
                const std::vector<FourSignalStep>& steps = entry.signals.at(signal_name);
                // validate the step count matches the resolved step count
                if (steps.size() != step_count) {
                    // log the error
                    spdlog::error("invalid Xyce FOUR file: inconsistent step count for signal '{}' in '{}': expected {}, found {}", signal_name, matching_files[0].string(), step_count, steps.size());
                    // exit
                    return std::nullopt;
                }
                // strip the braces from the signal name
                const std::string clean_name = strip_brace_ends(signal_name);
                // infer the spectrum unit from the raw file expression manager
                const std::string unit = expression_manager != nullptr ? expression_manager->infer_unit(clean_name) : "";
                // concatenated complex spectrum across all steps
                std::vector<std::complex<double>> spectrum_data;
                // per step metadata
                std::vector<std::unordered_map<std::string, std::string>> metadata_steps;
                // reserve capacity
                spectrum_data.reserve(steps.size() * abscissa_data.size());
                metadata_steps.reserve(steps.size());
                // process the steps
                for (const auto& step : steps) {
                    // append the complex spectrum of this step
                    spectrum_data.insert(spectrum_data.end(), step.spectrum->begin(), step.spectrum->end());
                    // append the step metadata
                    metadata_steps.push_back(*step.metadata);
                }
                // create the complex spectrum expression
                expressions.emplace_back(Expression<std::complex<double>>(clean_name, std::move(spectrum_data), step_slices, unit, "FOUR", metadata_steps));
            }
            // abscissa value ranges (dc to the last harmonic) for every step
            std::vector<std::pair<double, double>> value_ranges;
            value_ranges.reserve(step_count);
            for (size_t idx = 0; idx < step_count; ++idx)
                value_ranges.emplace_back(abscissa_data.front(), abscissa_data.back());
            // create the step information; the analysis step information is
            // carried over when provided, otherwise the files define a
            // step-less single-block dataset
            StepInformation four_step_information(step_information != nullptr ? step_information->keys() : std::vector<std::string>{}, step_information != nullptr ? step_information->values() : std::vector<std::vector<double>>{}, std::move(value_ranges));
            // create the expression manager
            ExpressionManager four_expression_manager(expressions, step_slices);
            // file metadata
            std::unordered_map<std::string, std::string> metadata{
                {"Fundamental Frequency", std::to_string(key.fundamental)},
                {"No. Harmonics", std::to_string(key.harmonics)},
            };
            // build the title from the fundamental frequency (integer Hz with no trailing zeros)
            std::string four_title = "FOUR: " + std::to_string(static_cast<long long>(key.fundamental)) + " Hz";
            // create the output file
            output_files.push_back(std::make_shared<XyceOutputFile>(matching_files[0], std::move(four_title), true, std::move(four_step_information), PlotType::FOURIER, AbscissaScale::LINEAR, std::move(four_expression_manager), nullptr, std::vector<std::vector<std::string>>{}, std::move(metadata)));
        }
        // return the output files
        return output_files;
    }
} // namespace

std::optional<std::vector<std::shared_ptr<XyceOutputFile>>> xyce_four_file_parser(const std::filesystem::path& file_pattern, const StepInformation* step_information, ExpressionManager* expression_manager) {
    // record the start time
    auto start_time = std::chrono::steady_clock::now();
    // find all files matching the pattern
    std::vector<std::filesystem::path> matching_files = glob_matching_files(file_pattern);
    // check no files matched
    if (matching_files.empty()) {
        // log the error
        spdlog::error("No Xyce FOUR files found matching pattern: {}", file_pattern.string());
        return std::nullopt;
    }
    // sort the files by their four numeric suffix
    std::stable_sort(matching_files.begin(), matching_files.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) { return get_four_index(lhs) < get_four_index(rhs); });
    // accumulated signals grouped by fundamental frequency
    std::map<FourKey, FourEntry> signals;
    // process each matching file
    for (const auto& filepath : matching_files) {
        // log the information
        spdlog::info("Loading Xyce FOUR file: {}", filepath.string());
        // parse the file into the accumulator
        if (!parse_four_file(filepath, signals))
            return std::nullopt;
    }
    // build the output files
    auto output_files = build_output_files(matching_files, step_information, expression_manager, signals);
    if (output_files)
        spdlog::info("Successfully parsed Xyce FOUR files: {}, elapsed time: {}ms", matching_files.size(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count());
    // return the output files
    return output_files;
}
