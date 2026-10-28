#pragma once

#include <string_view>

// check whether a function name matches the S/Z/Y/H NN network parameter pattern
bool is_network_parameter_probe_name(std::string_view name);
