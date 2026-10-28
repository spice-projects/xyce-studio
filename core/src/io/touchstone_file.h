#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "core/step_information.h"
#include "xyce_output_file.h"

std::optional<std::shared_ptr<XyceOutputFile>> touchstone_file_parser(const std::filesystem::path& filename, const StepInformation* step_info = nullptr);
