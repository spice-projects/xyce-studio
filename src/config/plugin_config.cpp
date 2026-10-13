#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <spdlog/spdlog.h>

#include "../core/util.h"
#include "plugin_config.h"

namespace
{
    // escape a string value for JSON serialization
    std::string json_escape(std::string_view str) {
        // allocate output buffer
        std::string result;
        // reserve space to avoid frequent reallocations
        result.reserve(str.size() + 16);
        // escape special JSON characters
        for (char c : str) {
            if (c == '\\') {
                result += "\\\\";
            }
            else if (c == '"') {
                result += "\\\"";
            }
            else if (c == '\n') {
                result += "\\n";
            }
            else if (c == '\r') {
                result += "\\r";
            }
            else if (c == '\t') {
                result += "\\t";
            }
            else {
                result += c;
            }
        }
        return result;
    }

    // parse a JSON string value starting at an opening quote position
    std::optional<std::string> parse_json_string(std::string_view content, size_t quote_pos) {
        // verify the start character is an opening quote
        if (quote_pos >= content.size() || content[quote_pos] != '"')
            return std::nullopt;
        // output parsed string
        std::string result;
        // escape state tracker
        bool escaping = false;
        // iterate characters following the opening quote
        for (size_t i = quote_pos + 1; i < content.size(); ++i) {
            char c = content[i];
            if (escaping) {
                if (c == '"') {
                    result += '"';
                }
                else if (c == '\\') {
                    result += '\\';
                }
                else if (c == 'n') {
                    result += '\n';
                }
                else if (c == 'r') {
                    result += '\r';
                }
                else if (c == 't') {
                    result += '\t';
                }
                else {
                    result += c;
                }
                escaping = false;
            }
            else if (c == '\\') {
                escaping = true;
            }
            else if (c == '"') {
                // reached matching unescaped closing quote
                return result;
            }
            else {
                result += c;
            }
        }
        return std::nullopt;
    }

    // locate a key in serialized JSON content, accepting the legacy spelling of a key
    std::optional<std::string_view> locate_key(std::string_view content, std::string_view key, std::string_view legacy_key) {
        // search for the current key spelling
        size_t key_pos = content.find(key);
        // fall back to the legacy spelling when the current one is absent
        if (key_pos == std::string_view::npos && !legacy_key.empty()) {
            // search for the legacy key spelling
            key_pos = content.find(legacy_key);
            // neither spelling is present
            if (key_pos == std::string_view::npos)
                return std::nullopt;
            // continue after the legacy key
            return content.substr(key_pos + legacy_key.size());
        }
        // the current spelling is absent and there is no legacy fallback
        if (key_pos == std::string_view::npos)
            return std::nullopt;
        // continue after the current key
        return content.substr(key_pos + key.size());
    }

    // resolve the value position of a key in serialized JSON content
    std::optional<size_t> locate_value(std::string_view content, std::string_view key, std::string_view legacy_key) {
        // find the key
        const auto after_key = locate_key(content, key, legacy_key);
        // the key is absent
        if (!after_key.has_value())
            return std::nullopt;
        // find the colon after the key
        const auto colon_pos = content.find(':', after_key->data() - content.data());
        // the key carries no value
        if (colon_pos == std::string_view::npos)
            return std::nullopt;
        // skip the whitespace in front of the value
        size_t value_pos = colon_pos + 1;
        while (value_pos < content.size() && std::isspace(static_cast<unsigned char>(content[value_pos])))
            value_pos++;
        // the value position
        return value_pos;
    }

    // extract a JSON string value for the given key from serialized content
    std::optional<std::string> extract_json_string(std::string_view content, std::string_view key, std::string_view legacy_key = "") {
        // find the value position of the key
        const auto value_pos = locate_value(content, key, legacy_key);
        // the key is absent or carries no value
        if (!value_pos.has_value())
            return std::nullopt;
        // find the opening quote of the value
        const auto quote_pos = content.find('"', *value_pos);
        // the value carries no quotes
        if (quote_pos == std::string_view::npos)
            return std::nullopt;
        // parse the quoted string
        return parse_json_string(content, quote_pos);
    }

    // extract a JSON boolean value for the given key from serialized content
    std::optional<bool> extract_json_bool(std::string_view content, std::string_view key) {
        // find the value position of the key
        const auto value_pos = locate_value(content, key, "");
        // the key is absent or carries no value
        if (!value_pos.has_value())
            return std::nullopt;
        // the remaining content from the value position
        const auto value = content.substr(*value_pos);
        // a true literal
        if (value.starts_with("true"))
            return true;
        // a false literal
        if (value.starts_with("false"))
            return false;
        // the value is not a boolean
        return std::nullopt;
    }

    // extract a JSON unsigned integer value for the given key from serialized content
    std::optional<unsigned> extract_json_unsigned(std::string_view content, std::string_view key) {
        // find the value position of the key
        const auto value_pos = locate_value(content, key, "");
        // the key is absent or carries no value
        if (!value_pos.has_value())
            return std::nullopt;
        // the offset of the first character that cannot be part of the value
        size_t value_end = *value_pos;
        while (value_end < content.size() && std::isdigit(static_cast<unsigned char>(content[value_end])))
            value_end++;
        // the digits of the value
        const auto digits = content.substr(*value_pos, value_end - *value_pos);
        // the value carries no digits
        if (digits.empty())
            return std::nullopt;
        // the parsed value
        unsigned parsed = 0;
        // convert the digits, a value the platform cannot represent is rejected
        const auto converted = std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
        // reject a partial conversion
        if (converted.ec != std::errc{} || converted.ptr != digits.data() + digits.size())
            return std::nullopt;
        return parsed;
    }

    // resolve the cross-platform path to the plugin configuration file
    std::filesystem::path get_config_file_path() {
#if defined(_WIN32)
        // check APPDATA environment variable on Windows
        if (const auto appdata = get_environment_variable("APPDATA"); appdata.has_value() && !appdata->empty())
            return std::filesystem::path(*appdata) / "xyce-studio" / "config.json";
        // check USERPROFILE environment variable as fallback on Windows
        if (const auto userprofile = get_environment_variable("USERPROFILE"); userprofile.has_value() && !userprofile->empty())
            return std::filesystem::path(*userprofile) / "AppData" / "Roaming" / "xyce-studio" / "config.json";
#elif defined(__APPLE__)
        // check XDG_CONFIG_HOME first if explicitly set
        if (const auto xdg = get_environment_variable("XDG_CONFIG_HOME"); xdg.has_value() && !xdg->empty())
            return std::filesystem::path(*xdg) / "xyce-studio" / "config.json";
        // use standard macOS Preferences directory
        if (const auto home = get_environment_variable("HOME"); home.has_value() && !home->empty())
            return std::filesystem::path(*home) / "Library" / "Preferences" / "xyce-studio" / "config.json";
#else
        // check XDG_CONFIG_HOME on POSIX/Linux
        if (const auto xdg = get_environment_variable("XDG_CONFIG_HOME"); xdg.has_value() && !xdg->empty())
            return std::filesystem::path(*xdg) / "xyce-studio" / "config.json";
        // fallback to ~/.config on POSIX/Linux
        if (const auto home = get_environment_variable("HOME"); home.has_value() && !home->empty())
            return std::filesystem::path(*home) / ".config" / "xyce-studio" / "config.json";
#endif
        // fallback to current working directory
        return std::filesystem::path{"config.json"};
    }

    // iterate PATH entries looking for a file named "Xyce" (or "Xyce.exe" on Windows)
    std::string search_path_for_xyce() {
        // query the PATH environment variable
        const auto path_env = get_environment_variable("PATH");
        if (!path_env.has_value() || path_env->empty())
            return {};
#if defined(_WIN32)
        // Windows uses semicolon as path separator
        constexpr char separator = ';';
#else
        // POSIX uses colon as path separator
        constexpr char separator = ':';
#endif
        // split PATH entries by separator
        const auto directories = split_by(*path_env, separator);
        // iterate over each directory in PATH
        for (const auto dir_view : directories) {
            if (dir_view.empty())
                continue;
            const std::filesystem::path dir_path(dir_view);
            std::error_code ec;
#if defined(_WIN32)
            // check for Xyce.exe on Windows
            const auto candidate_exe = dir_path / "Xyce.exe";
            if (std::filesystem::is_regular_file(candidate_exe, ec))
                return candidate_exe.string();
            ec.clear();
            // check for Xyce on Windows
            const auto candidate = dir_path / "Xyce";
            if (std::filesystem::is_regular_file(candidate, ec))
                return candidate.string();
#else
            // check for Xyce on POSIX
            const auto candidate = dir_path / "Xyce";
            if (std::filesystem::is_regular_file(candidate, ec)) {
                // check if the file is executable
                if (::access(candidate.c_str(), X_OK) == 0)
                    return candidate.string();
            }
#endif
        }
        return {};
    }
} // namespace

PluginConfig::PluginConfig(std::string xyce_executable_path) :
    m_xyce_executable_path(std::move(xyce_executable_path)) {}

PluginConfig PluginConfig::load() {
    // resolve configuration file path
    const auto config_path = get_config_file_path();
    std::error_code ec;
    // read configuration from disk if the file exists
    if (std::filesystem::is_regular_file(config_path, ec)) {
        // open the config file for reading
        std::ifstream file(config_path);
        if (file.is_open()) {
            // read the entire file into a string buffer
            std::stringstream buffer;
            buffer << file.rdbuf();
            // the serialized content every field is read from
            const auto content = buffer.str();
            // attempt to extract the Xyce executable path from the JSON content, the legacy key spelling still loads an older file
            const auto stored_path = extract_json_string(content, "\"xyce_executable_path\"", "\"XyceExecutablePath\"");
            // continue with discovery when the file holds no usable path
            if (!stored_path.has_value() || stored_path->empty()) {
                // fall through to discovery below
            }
            else {
                // the configuration read back from disk
                PluginConfig config(*stored_path);
                // the parallel run defaults to off, an older file carries no key for it
                config.m_parallel_enabled = extract_json_bool(content, "\"parallel_enabled\"").value_or(false);
                // the launcher path is optional, a parallel run with none is left invalid on purpose
                config.m_mpi_launcher_path = extract_json_string(content, "\"mpi_launcher_path\"").value_or("");
                // a process count of one would not be a parallel run at all, so an absent or unusable key keeps the serial default
                config.m_process_count = extract_json_unsigned(content, "\"process_count\"").value_or(1);
                // the additional launcher arguments are optional
                config.m_mpi_launcher_arguments = extract_json_string(content, "\"mpi_launcher_arguments\"").value_or("");
                // log information
                spdlog::debug("Loaded Xyce executable path from config: {}", *stored_path);
                // return the stored path
                return config;
            }
        }
    }
    // discover the Xyce executable path when not found in config
    auto discovered = discover_xyce_executable();
    if (!discovered.empty()) {
        // log information
        spdlog::debug("Discovered Xyce executable path: {}", discovered);
        // return the discovered path
        return PluginConfig(std::move(discovered));
    }
    // log information
    spdlog::info("No Xyce executable discovered");
    // exit
    return PluginConfig();
}

PluginConfig PluginConfig::default_config() { return PluginConfig(discover_xyce_executable()); }

void PluginConfig::save() const {
    // resolve configuration file path
    const auto config_path = get_config_file_path();
    std::error_code ec;
    // ensure the parent directory exists
    std::filesystem::create_directories(config_path.parent_path(), ec);
    if (ec) {
        // log warning
        spdlog::warn("Failed to create config directory: {}", ec.message());
        return;
    }
    // open the config file for writing
    std::ofstream file(config_path);
    if (!file.is_open()) {
        // log warning
        spdlog::warn("Failed to save Xyce executable path to config");
        return;
    }
    // serialize configuration as JSON
    file << "{\n";
    file << "    \"xyce_executable_path\": \"" << json_escape(m_xyce_executable_path) << "\",\n";
    file << "    \"parallel_enabled\": " << (m_parallel_enabled ? "true" : "false") << ",\n";
    file << "    \"mpi_launcher_path\": \"" << json_escape(m_mpi_launcher_path) << "\",\n";
    file << "    \"process_count\": " << m_process_count << ",\n";
    file << "    \"mpi_launcher_arguments\": \"" << json_escape(m_mpi_launcher_arguments) << "\"\n";
    file << "}\n";
    // log information
    spdlog::debug("Saved Xyce executable path to config: {}", m_xyce_executable_path);
}

bool PluginConfig::is_parallel_configuration_valid() const {
    // a serial run has no parallel configuration to validate
    if (!m_parallel_enabled)
        return true;
    // reject an empty launcher path before filesystem checks
    if (m_mpi_launcher_path.empty())
        return false;
    // check if the launcher exists and is a regular file
    std::error_code ec;
    const auto fs_status = std::filesystem::status(m_mpi_launcher_path, ec);
    // reject paths that do not reference an existing regular file
    if (ec || !std::filesystem::is_regular_file(fs_status))
        return false;
#if defined(_WIN32)
    // on Windows, a regular file that exists is launchable
    const bool launchable = true;
#else
    // require executable permission on POSIX
    const bool launchable = ::access(m_mpi_launcher_path.c_str(), X_OK) == 0;
#endif
    // a launcher that cannot be executed cannot launch anything
    if (!launchable)
        return false;
    // a single process is not a parallel run, the launcher would still start one copy of Xyce
    return m_process_count >= 2;
}

bool PluginConfig::is_xyce_executable_valid() const {
    // reject empty paths before filesystem checks
    if (m_xyce_executable_path.empty())
        return false;
    // check if the path exists and is a regular file
    std::error_code ec;
    const auto fs_status = std::filesystem::status(m_xyce_executable_path, ec);
    // reject paths that do not reference an existing regular file
    if (ec || !std::filesystem::is_regular_file(fs_status))
        return false;
#if defined(_WIN32)
    // on Windows, a regular file that exists is valid
    return true;
#else
    // require executable permission on POSIX
    return ::access(m_xyce_executable_path.c_str(), X_OK) == 0;
#endif
}

std::string PluginConfig::discover_xyce_executable() {
    // check PATH first (covers source builds, Spack, and custom installs)
    std::string found = search_path_for_xyce();
    if (!found.empty()) {
        // log information
        spdlog::debug("Found Xyce on PATH: {}", found);
        // exit
        return found;
    }
    // fall back to well-known binary-installer directories
#if defined(_WIN32)
    // check "C:\Program Files" for XyceNF_* directories (binary installer)
    const std::filesystem::path base = "C:\\Program Files";
    const std::string prefix = "XyceNF_";
    const std::string exe_rel = "bin\\Xyce.exe";
#else
    // check /usr/local for XyceNF_* directories (binary installer)
    const std::filesystem::path base = "/usr/local";
    const std::string prefix = "XyceNF_";
    const std::string exe_rel = "bin/Xyce";
#endif
    std::error_code ec;
    if (std::filesystem::exists(base, ec) && std::filesystem::is_directory(base, ec)) {
        std::string best;
        // iterate over subdirectories matching the prefix
        for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
            if (ec)
                break;
            if (entry.is_directory(ec)) {
                const std::string name = entry.path().filename().string();
                if (name.starts_with(prefix)) {
                    // keep the highest-versioned directory name
                    if (best.empty() || name > best)
                        best = name;
                }
            }
        }
        // check if a matching directory was found
        if (!best.empty()) {
            const auto candidate = base / best / exe_rel;
            ec.clear();
            if (std::filesystem::is_regular_file(candidate, ec)) {
#if !defined(_WIN32)
                if (::access(candidate.c_str(), X_OK) == 0) {
                    spdlog::debug("Found Xyce in install directory: {}", candidate.string());
                    return candidate.string();
                }
#else
                spdlog::debug("Found Xyce in install directory: {}", candidate.string());
                return candidate.string();
#endif
            }
        }
    }
    // log information
    spdlog::warn("Xyce executable not found via discovery");
    // exit with empty string
    return {};
}
