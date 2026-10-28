#pragma once

#include <string>
#include <vector>

#include "simulation/dc_simulation_parameters.h"

// one editable row of the DC sweep dialog's nested-sweep table;
// a view-layer projection of DcSweep with string-only fields
struct DcSweepRowFields
{
    // sweep variable name
    std::string variable;
    // start value (LIN/DEC/OCT sweeps)
    std::string start;
    // stop value (LIN/DEC/OCT sweeps)
    std::string stop;
    // step size (LIN sweeps)
    std::string step;
    // points per decade/octave (DEC/OCT sweeps)
    std::string points;
    // space-separated list of values (LIST sweeps)
    std::string list_values;
};

// project sweep entries into editable dialog rows
[[nodiscard]] std::vector<DcSweepRowFields> dc_sweep_rows_from_sweeps(const std::vector<DcSweep>& sweeps);

// rebuild sweep entries from dialog rows for a sweep mode;
// fully blank rows are dropped, mode-specific fields are normalized per RG §2.1.3
[[nodiscard]] std::vector<DcSweep> dc_sweeps_from_rows(const std::vector<DcSweepRowFields>& rows, const std::string& sweep_mode);
