#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "option_parameters.h"

// the managed .OPTIONS packages, in the fixed order used by the options dialogs
enum class OptionPackage
{
    DEVICE,
    TIMEINT,
    NONLIN,
    NONLIN_TRAN,
    LINSOL,
    LINSOL_AC,
    LOCA,
    PARSER,
    DIAGNOSTIC,
    DIST,
    MEASURE,
    FFT,
    OUTPUT,
    RESTART,
    SAMPLES,
    EMBEDDEDSAMPLES
};

// number of managed packages; also the exclusive upper bound of OptionPackage
constexpr size_t OPTION_PACKAGE_COUNT = 16;

// editing metadata for one documented option key
struct OptionKeyInfo
{
    // allowed values for closed-choice keys; empty for free-text keys
    std::vector<std::string> choices;
    // reference guide default shown as the editor placeholder; empty when none is documented
    std::string default_value;
};

// RG-documented option keys for the package, in documentation order (Xyce Reference Guide 2.1.25)
[[nodiscard]] const std::vector<std::string>& option_package_catalog(OptionPackage package);

// choices and reference default for a key of the package; unknown keys report empty metadata
[[nodiscard]] OptionKeyInfo option_key_info(OptionPackage package, const std::string& key);

// reference to the OptionParameters member that backs the package
[[nodiscard]] std::map<std::string, std::string>& package_options(OptionParameters& options, OptionPackage package);
[[nodiscard]] const std::map<std::string, std::string>& package_options(const OptionParameters& options, OptionPackage package);
