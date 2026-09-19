#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "xyce_output_file.h"

// parse a .prn Xyce print output file into an XyceOutputFile
std::optional<std::shared_ptr<XyceOutputFile>> xyce_prn_file_parser(const std::filesystem::path& filename);