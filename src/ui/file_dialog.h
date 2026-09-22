#pragma once

#include <filesystem>
#include <optional>
#include <string>

// platform-native open file dialog backed by nativefiledialog-extended (btzy/nativefiledialog-extended)
class FileDialog
{
public:
    // comma-separated lowercase extension list the open dialog filters by: the
    // netlist format plus every analysis output format the presenter loads
    // (.raw, .prn, .csd and the FORMAT=CSV .csv variants)
    [[nodiscard]] static std::string open_file_extensions();

    // show a native dialog to select a Xyce input/output file; returns the
    // selected path or std::nullopt when the user cancels
    [[nodiscard]] static std::optional<std::filesystem::path> open_xyce_file();

    // show a native dialog to select the Xyce executable; returns the selected
    // path or std::nullopt when the user cancels
    [[nodiscard]] static std::optional<std::filesystem::path> open_xyce_executable();
};
