#include "simulation/option_editor.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

#include "core/util.h"
#include "netlist/netlist.h"

const std::vector<OptionPackage>& option_editor::dialog_packages() {
    // the packages the analysis dialog does not edit
    static const std::vector<OptionPackage> PACKAGES = {OptionPackage::DEVICE, OptionPackage::LINSOL, OptionPackage::PARSER, OptionPackage::DIAGNOSTIC, OptionPackage::DIST, OptionPackage::MEASURE, OptionPackage::RESTART};
    return PACKAGES;
}

std::string_view option_editor::package_name(const OptionPackage package) {
    // the spelling the netlist writes after .OPTIONS
    switch (package) {
    case OptionPackage::DEVICE:
        return "DEVICE";
    case OptionPackage::TIMEINT:
        return "TIMEINT";
    case OptionPackage::NONLIN:
        return "NONLIN";
    case OptionPackage::NONLIN_TRAN:
        return "NONLIN-TRAN";
    case OptionPackage::LINSOL:
        return "LINSOL";
    case OptionPackage::LINSOL_AC:
        return "LINSOL-AC";
    case OptionPackage::LOCA:
        return "LOCA";
    case OptionPackage::PARSER:
        return "PARSER";
    case OptionPackage::DIAGNOSTIC:
        return "DIAGNOSTIC";
    case OptionPackage::DIST:
        return "DIST";
    case OptionPackage::MEASURE:
        return "MEASURE";
    case OptionPackage::FFT:
        return "FFT";
    case OptionPackage::OUTPUT:
        return "OUTPUT";
    case OptionPackage::RESTART:
        return "RESTART";
    case OptionPackage::SAMPLES:
        return "SAMPLES";
    case OptionPackage::EMBEDDEDSAMPLES:
        return "EMBEDDEDSAMPLES";
    }
    // a package outside the enumeration writes nothing
    return {};
}

int option_editor::row_choice_index(const OptionRow& row) {
    // a row without a list edits free text
    if (row.choices.empty())
        return -1;
    // a bare flag selects the marker that keeps it
    if (row.flag && row.value.empty()) {
        // the marker the row offers
        const auto marker = std::find(row.choices.begin(), row.choices.end(), FLAG_CHOICE);
        // a row without the marker falls back to the free text editor
        return marker == row.choices.end() ? -1 : static_cast<int>(std::distance(row.choices.begin(), marker));
    }
    // an empty value selects the entry that clears the option
    if (row.value.empty())
        return 0;
    // the entry that names the value
    const auto found = std::find(row.choices.begin(), row.choices.end(), row.value);
    // an undocumented value falls back to the free text editor
    return found == row.choices.end() ? -1 : static_cast<int>(std::distance(row.choices.begin(), found));
}

std::vector<option_editor::OptionRow> option_editor::build_rows(const OptionPackage package, const std::map<std::string, std::string>& loaded) {
    // the documented keys decide the row order
    const std::vector<std::string>& catalog = option_package_catalog(package);
    // one row per documented key plus any netlist-only key
    std::vector<OptionRow> rows;
    rows.reserve(catalog.size() + loaded.size());
    // every documented key takes a row, pre-filled when the netlist carries it
    for (const std::string& key : catalog) {
        // the value the netlist carries for this key
        const auto found = loaded.find(key);
        // a key the netlist omits opens empty, which the editor offers to set
        const bool present = found != loaded.end();
        // build the row the editor shows
        OptionRow row;
        row.key = key;
        row.value = present ? found->second : std::string{};
        row.flag = present && row.value.empty();
        // the reference metadata the editor hints at and offers
        const OptionKeyInfo info = option_key_info(package, key);
        row.default_value = info.default_value;
        // a key with choices or a loaded bare flag opens the choice list
        if (!info.choices.empty() || row.flag) {
            // the first entry clears the option
            row.choices.emplace_back(DEFAULT_CHOICE);
            // a bare flag keeps the marker that writes it without a value
            if (row.flag)
                row.choices.emplace_back(FLAG_CHOICE);
            // the documented choices follow in reference order
            row.choices.insert(row.choices.end(), info.choices.begin(), info.choices.end());
        }
        // the selection the current value names
        row.choice_index = row_choice_index(row);
        rows.push_back(std::move(row));
    }
    // a netlist key the catalog does not document follows, so no loaded option is hidden
    for (const auto& [key, value] : loaded) {
        // a documented key already owns its row
        if (std::find(catalog.begin(), catalog.end(), key) != catalog.end())
            continue;
        // build the free text row the unknown key edits
        OptionRow row;
        row.key = key;
        row.value = value;
        row.flag = value.empty();
        row.default_value = option_key_info(package, key).default_value;
        rows.push_back(std::move(row));
    }
    // hand the editor the rows it shows
    return rows;
}

std::map<std::string, std::string> option_editor::apply_rows(const std::vector<OptionRow>& rows) {
    // the options the rows declare
    std::map<std::string, std::string> options;
    // every row with a key contributes one option
    for (const OptionRow& row : rows) {
        // the key reaches the editor uppercased from the catalog and the netlist parser
        const std::string key = to_upper(trim(row.key));
        // the value is written verbatim apart from surrounding whitespace
        const std::string value = trim(row.value);
        // a row without a key cannot form an option
        if (key.empty())
            continue;
        // a valued row writes KEY=VALUE
        if (!value.empty())
            options[key] = value;
        // an empty row keeps only the keys the netlist carried as bare flags
        else if (row.flag)
            options[key] = "";
    }
    // hand the options to the serializer
    return options;
}

OptionParameters option_editor::assemble(const OptionParameters& current, const std::vector<std::vector<OptionRow>>& rows) {
    // start from the options the editor opened with, so only its own packages are rewritten
    OptionParameters options = current;
    // the packages the editor shows, in card order
    const std::vector<OptionPackage>& packages = dialog_packages();
    // every package the editor showed replaces the values it opened with
    for (std::size_t index = 0; index < packages.size() && index < rows.size(); ++index)
        package_options(options, packages[index]) = apply_rows(rows[index]);
    // hand back the options the editor declares
    return options;
}

namespace
{
    // the leading whitespace-free view of one physical line
    std::string_view trimmed_left(const std::string_view line) {
        // the first byte that is not whitespace
        std::size_t start = 0;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])) != 0)
            start++;
        return line.substr(start);
    }

    // the first two whitespace separated tokens of a directive, uppercased
    std::vector<std::string> leading_tokens(const std::string_view line, const std::size_t count) {
        // one token at a time
        std::vector<std::string> tokens;
        std::size_t start = 0;
        while (start < line.size() && tokens.size() < count) {
            // the next separator
            const std::size_t separator = line.find_first_of(" \t", start);
            // the token the range names
            tokens.push_back(to_upper(std::string(line.substr(start, separator == std::string_view::npos ? std::string_view::npos : separator - start))));
            // a line with no separator ends with its first token
            if (separator == std::string_view::npos)
                break;
            // the next token starts after the whitespace
            start = line.find_first_not_of(" \t", separator);
            // a trailing separator names no token
            if (start == std::string_view::npos)
                break;
        }
        return tokens;
    }

    // whether the line opens an .OPTIONS directive the editor owns
    bool is_editor_option_line(const std::string_view line) {
        // only a two token .OPTIONS directive names a package
        const std::vector<std::string> tokens = leading_tokens(line, 2);
        // anything shorter is not a package directive
        if (tokens.size() < 2 || tokens[0] != ".OPTIONS")
            return false;
        // the package token the directive names
        const std::string_view package(tokens[1]);
        // only the packages the editor shows are rewritten
        return std::any_of(option_editor::dialog_packages().begin(), option_editor::dialog_packages().end(), [package](const OptionPackage candidate) { return option_editor::package_name(candidate) == package; });
    }

    // whether the line is the .END marker
    bool is_end_line(const std::string_view line) {
        // the marker is the first token of the line
        const std::string_view trimmed = trimmed_left(line);
        // an .END followed by nothing or by whitespace closes the netlist
        return trimmed.size() >= 4 && std::string_view(trimmed.substr(0, 4)) == ".END" && (trimmed.size() == 4 || std::isspace(static_cast<unsigned char>(trimmed[4])) != 0);
    }
} // namespace

std::string option_editor::rewrite_directives(const std::string_view netlist, const OptionParameters& options) {
    // the directive lines the editor owns, in serializer order
    std::vector<std::string> block;
    for (const std::string& directive : options.to_xyce_directives(NetlistTopology{})) {
        // only the packages the editor shows are rewritten here
        if (is_editor_option_line(directive))
            block.emplace_back(directive);
    }
    // the lines the rewrite keeps
    std::string body;
    // whether the logical line under construction belongs to an editor package
    bool dropping = false;
    // the offset the .END marker starts at, or no offset when the netlist has none
    std::size_t end_offset = std::string::npos;
    // walk the physical lines, because a + line continues the directive before it
    std::size_t start = 0;
    while (start < netlist.size()) {
        // the end of this physical line without its terminator
        const std::size_t newline = netlist.find('\n', start);
        const std::size_t stop = newline == std::string_view::npos ? netlist.size() : newline;
        // the line the walk reads
        const std::string_view line = trimmed_left(netlist.substr(start, stop - start));
        // a continuation belongs to the logical line before it
        if (!line.empty() && line.front() == '+') {
            // a kept continuation stays with its directive
            if (!dropping) {
                body.append(netlist.substr(start, stop - start));
                body.push_back('\n');
            }
        }
        // every other line opens a logical line of its own
        else {
            // the package this line names, which decides whether it is rewritten
            dropping = is_editor_option_line(line);
            // the .END marker anchors the insertion of the rewritten block
            if (!dropping && end_offset == std::string::npos && is_end_line(line))
                end_offset = body.size();
            // a kept line stays byte for byte
            if (!dropping) {
                body.append(netlist.substr(start, stop - start));
                body.push_back('\n');
            }
        }
        // the next line starts after the terminator
        start = stop + 1;
    }
    // the text the rewritten directives form
    std::string directives_text;
    for (const std::string& directive : block) {
        directives_text += directive;
        directives_text.push_back('\n');
    }
    // a netlist with the marker takes the block before it
    if (end_offset != std::string::npos) {
        // an empty block leaves the body without the options
        if (!block.empty()) {
            // a blank line separates the block from the body above it
            body.insert(end_offset, "\n" + directives_text);
        }
    }
    // a netlist without the marker takes the block at its end
    else if (!block.empty()) {
        // a blank line separates the block from the body above it
        if (!body.empty())
            body.push_back('\n');
        // the block closes the netlist
        body += directives_text;
        body += ".END\n";
    }
    // hand back the rewritten netlist
    return body;
}
