#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "../core/step_information.h"
#include "../expression/expression_manager.h"
#include "xyce_output_file.h"

// parse the FFT calculation output files matching the pattern; the analysis
// step information maps the files' STEP markers onto the analysis output's
// step slices when provided — a run without a .PRINT directive produces no
// analysis output, the files' own step structure then drives the parsing
std::optional<std::vector<std::shared_ptr<XyceOutputFile>>> xyce_fft_file_parser(const std::filesystem::path& file_pattern, const StepInformation* step_information = nullptr, ExpressionManager* expression_manager = nullptr);
