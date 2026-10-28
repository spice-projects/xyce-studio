#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "xyce_output_file.h"

// parse a .dat Xyce print output file (FORMAT=TECPLOT) into an XyceOutputFile
std::optional<std::shared_ptr<XyceOutputFile>> xyce_tecplot_file_parser(const std::filesystem::path& filename);
