#include "dc_sweep_rows.h"

#include <algorithm>
#include <string_view>

#include "core/util.h"

namespace
{
    // true when the text is empty or whitespace-only
    [[nodiscard]] bool is_blank(std::string_view text) { return text.find_first_not_of(" \t\r\n") == std::string_view::npos; }

    // join strings with a single space delimiter
    [[nodiscard]] std::string join_spaced(const std::vector<std::string>& parts) {
        std::string result;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i != 0)
                result += " ";
            result += parts[i];
        }
        return result;
    }
} // namespace

std::vector<DcSweepRowFields> dc_sweep_rows_from_sweeps(const std::vector<DcSweep>& sweeps) {
    // project each sweep entry into one editable dialog row
    std::vector<DcSweepRowFields> rows;
    rows.reserve(sweeps.size());
    for (const auto& s : sweeps) {
        rows.push_back(DcSweepRowFields{s.variable, s.start, s.stop, s.step, s.points, join_spaced(s.list_values)});
    }
    return rows;
}

std::vector<DcSweep> dc_sweeps_from_rows(const std::vector<DcSweepRowFields>& rows, const std::string& sweep_mode) {
    // DATA sweeps carry no sweep tuples
    if (sweep_mode == "DATA")
        return {};
    std::vector<DcSweep> sweeps;
    for (const auto& row : rows) {
        // drop rows the user never filled in
        if (is_blank(row.variable) && is_blank(row.start) && is_blank(row.stop) && is_blank(row.step) && is_blank(row.points) && is_blank(row.list_values))
            continue;
        DcSweep sweep;
        sweep.variable = trim(row.variable);
        if (sweep_mode == "LIST") {
            // LIST rows carry only the sweep variable and its space-separated values
            for (const auto& value : tokenize(row.list_values)) {
                sweep.list_values.push_back(std::string(value));
            }
        }
        else {
            sweep.start = trim(row.start);
            sweep.stop = trim(row.stop);
            if (sweep_mode == "LIN") {
                // tolerate a step typed into the points field after a mode switch
                sweep.step = is_blank(row.step) ? trim(row.points) : trim(row.step);
            }
            else {
                // tolerate points typed into the step field after a mode switch
                sweep.points = is_blank(row.points) ? trim(row.step) : trim(row.points);
            }
        }
        sweeps.push_back(std::move(sweep));
    }
    return sweeps;
}
