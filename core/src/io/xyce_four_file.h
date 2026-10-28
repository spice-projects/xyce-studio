#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "core/step_information.h"
#include "expression/expression_manager.h"
#include "xyce_output_file.h"

// parse the .FOUR Fourier analysis output files matching the pattern; every
// fundamental frequency becomes one output file holding a complex spectrum per
// signal, and every file contributes one step; the analysis step information
// labels the step slices when provided, otherwise the files' own order drives
// the dataset
std::optional<std::vector<std::shared_ptr<XyceOutputFile>>> xyce_four_file_parser(const std::filesystem::path& file_pattern, const StepInformation* step_information = nullptr, ExpressionManager* expression_manager = nullptr);
