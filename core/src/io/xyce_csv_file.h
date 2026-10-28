#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "xyce_output_file.h"

// parse a .csv Xyce print output file (FORMAT=CSV) into an XyceOutputFile
std::optional<std::shared_ptr<XyceOutputFile>> xyce_csv_file_parser(const std::filesystem::path& filename);
