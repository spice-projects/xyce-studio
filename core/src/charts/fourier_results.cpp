#include "charts/fourier_results.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <numbers>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "expression/expression.h"
#include "expression/expression_manager.h"
#include "io/xyce_output_file.h"

namespace
{
    // format one numeric cell like the .FOUR file prints it, in scientific notation with six decimals
    std::string format_value(const double value) {
        // the stream carries the column precision
        std::ostringstream text;
        // match the .four column formatting
        text << std::scientific << std::setprecision(6) << value;
        // hand the cell back
        return text.str();
    }

    // format the per-block THD as the percentage the file records, where a non-finite value reads as `n/a`
    std::string format_thd(const std::string& raw) {
        // the file records a percentage or the `nan` token, which the conversion reads without throwing
        double value = std::numeric_limits<double>::quiet_NaN();
        // a token the parser cannot read has no percentage either
        try {
            // read the whole token
            std::size_t used = 0;
            value = std::stod(raw, &used);
            // a token with trailing text is not a percentage
            if (used != raw.size())
                value = std::numeric_limits<double>::quiet_NaN();
        }
        catch (...) {
            // the conversion refused the token
        }
        // a non-finite value has no meaningful percentage
        if (!std::isfinite(value))
            return "n/a";
        // format with a readable precision and append the percent unit
        std::ostringstream text;
        // the percentage keeps four significant digits
        text << std::setprecision(4) << value << " %";
        // hand the cell back
        return text.str();
    }

    // format one derived cell, where a value the division cannot express reads as `n/a`
    std::string format_derived(const double value) {
        // a vanished fundamental has no ratio or offset to print
        if (!std::isfinite(value))
            return "n/a";
        // a finite value prints like every other cell
        return format_value(value);
    }

    // read one metadata entry, which a file that omits it reads as empty
    std::string metadata_value(const std::unordered_map<std::string, std::string>& step_metadata, const std::string& key) {
        // the entry the file wrote for this step
        const auto entry = step_metadata.find(key);
        // a missing entry reads as empty
        return entry != step_metadata.end() ? entry->second : std::string{};
    }
} // namespace

std::vector<fourier::SignalTable> fourier::build_tables(XyceOutputFile& file) {
    // the result blocks
    std::vector<SignalTable> tables;
    // the step structure and expressions of the file
    const StepInformation& steps = file.step_information();
    // the manager the file expressions resolve through
    ExpressionManager& manager = file.expression_manager();
    // the abscissa expression carries the harmonic frequency of every step
    Expression<double>& abscissa = manager.abscissa();
    // the number of steps the file holds
    const std::size_t step_count = steps.length();
    // the abscissa carries one slice per step, which bounds the walk below
    const std::size_t abscissa_steps = std::min(step_count, abscissa.step_count());
    // the step label is shown only for multi-step sweeps
    const bool show_step = step_count > 1;
    // the degrees per radian the phase column prints
    constexpr double DEGREES_PER_RADIAN = 180.0 / std::numbers::pi;
    // one block is emitted per signal and step, skipping the real expressions and the abscissa
    for (AnyExpression* any : manager.expressions()) {
        // keep only complex spectra
        auto* spectrum = std::get_if<Expression<std::complex<double>>>(any);
        // skip the abscissa and any real expression
        if (spectrum == nullptr)
            continue;
        // walk the steps both the spectrum and the abscissa describe
        const std::size_t spectrum_steps = std::min(abscissa_steps, spectrum->step_count());
        // walk every step of this signal
        for (std::size_t step = 0; step < spectrum_steps; ++step) {
            // the complex values of this step
            const std::span<const std::complex<double>> values = spectrum->step_data(step);
            // the harmonic frequencies of this step, which the parser builds from the same rows as the spectrum
            const std::span<const double> frequencies = abscissa.step_data(step);
            // start the block of this signal and step
            SignalTable table;
            // the signal name, which the parser already stripped of its braces
            table.signal = spectrum->name();
            // the step label only when several steps are present
            table.step = show_step ? "Step " + std::to_string(step + 1) : std::string();
            // a block without a recorded THD reports none rather than reading past the end
            table.thd = "n/a";
            // the parser stores one metadata map per step
            const std::vector<std::unordered_map<std::string, std::string>>& metadata = spectrum->metadata();
            // the settings and the THD the file recorded for this step
            if (step < metadata.size()) {
                // the THD entry the file wrote
                const auto entry = metadata[step].find("THD");
                // a recorded THD replaces the fallback
                if (entry != metadata[step].end())
                    table.thd = format_thd(entry->second);
                // the harmonic count the analysis reported
                table.harmonics = metadata_value(metadata[step], "No. Harmonics");
                // the interpolation grid points the analysis used
                table.gridsize = metadata_value(metadata[step], "Gridsize");
                // the interpolation type the analysis used
                table.interpolation = metadata_value(metadata[step], "Interpolation Type");
            }
            // the magnitude the normalized cells divide by, which is the first harmonic
            const double fundamental_magnitude = values.size() > 1 ? std::abs(values[1]) : 0.0;
            // the phase the normalized cells subtract, which is the first harmonic
            const double fundamental_phase = values.size() > 1 ? std::arg(values[1]) * DEGREES_PER_RADIAN : 0.0;
            // walk every harmonic written for this signal
            for (std::size_t harmonic = 0; harmonic < values.size() && harmonic < frequencies.size(); ++harmonic) {
                // build the row
                ResultRow row;
                // the harmonic index
                row.harmonic = static_cast<int>(harmonic);
                // the harmonic frequency from the abscissa
                row.frequency = format_value(frequencies[harmonic]);
                // the dc term is the mean Xyce prints, which keeps its sign instead of a modulus
                const bool dc_row = harmonic == 0;
                // the magnitude is the mean of the dc row and the complex modulus of every harmonic
                const double magnitude = dc_row ? values[harmonic].real() : std::abs(values[harmonic]);
                // the dc row carries no phase, which Xyce forces to zero
                const double phase = dc_row ? 0.0 : std::arg(values[harmonic]) * DEGREES_PER_RADIAN;
                // the magnitude cell mirrors the file column
                row.magnitude = format_value(magnitude);
                // the phase cell mirrors the file column
                row.phase = format_value(phase);
                // the file leaves the dc row of the normalized columns at zero
                row.norm_magnitude = dc_row ? format_value(0.0) : format_derived(magnitude / fundamental_magnitude);
                // the file leaves the dc row of the normalized columns at zero
                row.norm_phase = dc_row ? format_value(0.0) : format_derived(phase - fundamental_phase);
                // append the row
                table.rows.push_back(std::move(row));
            }
            // append the block
            tables.push_back(std::move(table));
        }
    }
    // hand the assembled blocks back
    return tables;
}
