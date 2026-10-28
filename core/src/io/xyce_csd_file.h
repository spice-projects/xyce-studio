#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "xyce_output_file.h"

// parse a .csd Xyce print output file (PROBE format) into an XyceOutputFile
std::optional<std::shared_ptr<XyceOutputFile>> xyce_csd_file_parser(const std::filesystem::path& filename);
