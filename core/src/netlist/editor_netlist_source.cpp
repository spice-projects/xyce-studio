#include <string>
#include <tuple>
#include <utility>

#include "editor_netlist_source.h"

EditorNetlistSource::EditorNetlistSource(std::function<std::string()> get_editor_text, std::filesystem::path file_path) :
    m_get_editor_text(std::move(get_editor_text)) {
    // create a file-based netlist source for the given path
    m_file_source = std::make_unique<FileNetlistSource>(std::move(file_path));
}

std::string EditorNetlistSource::title() const {
    // delegate the title to the file source
    return m_file_source->title();
}

bool EditorNetlistSource::is_read_only() const {
    // editor content can always be modified
    return false;
}

bool EditorNetlistSource::has_backing_file() const {
    // delegate to the file source: an empty path means the netlist is untitled
    return m_file_source->has_backing_file();
}

std::filesystem::path EditorNetlistSource::working_directory() const {
    // delegate the working directory to the file source
    return m_file_source->working_directory();
}

std::tuple<bool, std::string> EditorNetlistSource::load_netlist() {
    // first load reads the file content into the live text source
    if (!m_initialized) {
        // mark as initialized
        m_initialized = true;
        // load the netlist from the file source
        const auto [reloaded, content] = m_file_source->load_netlist();
        // an untitled source has no file to read (the file open failed): serve
        // the live editor text so a netlist typed from scratch is never lost
        if (!reloaded)
            return {false, m_get_editor_text()};
        // return the result of loading from the file source
        return {reloaded, content};
    }
    // subsequent loads reflect the live editor text
    return {false, m_get_editor_text()};
}

void EditorNetlistSource::save_netlist(const std::string& content) {
    // use the live editor text when no explicit content was provided
    const std::string text = content.empty() ? m_get_editor_text() : content;
    // delegate the save to the file source
    m_file_source->save_netlist(text);
}
