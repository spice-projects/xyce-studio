#include <fstream>
#include <string>
#include <tuple>

#include "file_netlist_source.h"

FileNetlistSource::FileNetlistSource(std::filesystem::path file_path) :
    m_file_path(std::move(file_path)) {}

std::string FileNetlistSource::title() const { return m_file_path.filename().string(); }

bool FileNetlistSource::is_read_only() const { return false; }

bool FileNetlistSource::has_backing_file() const { return !m_file_path.empty(); }

[[nodiscard]] std::filesystem::path FileNetlistSource::working_directory() const { return m_file_path.parent_path(); }

std::tuple<bool, std::string> FileNetlistSource::load_netlist() {
    // open netlist file
    std::ifstream file(m_file_path, std::ios::in);
    if (!file.is_open())
        return {false, {}};
    // read file content into string
    std::string content;
    content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // return true indicating it is a fresh read and the content of the file
    return {true, std::move(content)};
}

void FileNetlistSource::save_netlist(const std::string& content) {
    // open stream in write mode
    std::ofstream file(m_file_path, std::ios::out | std::ios::trunc);
    if (file.is_open()) {
        // write content into file
        file << content;
        // close stream
        file.close();
    }
}
