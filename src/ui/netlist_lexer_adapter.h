#pragma once

#include <memory>
#include <vector>

#include <slint.h>

#include <main_window.h>

#include "netlist/netlist_lexer.h"

// map a token type to a colour for the given theme; foreground carries the
// theme's default text colour for tokens that follow the editor text (nodes,
// plain text).  the palette matches the colour table from issue #210.
inline slint::Color token_type_to_color(NetlistTokenType type, bool dark_mode, slint::Color foreground) {
    if (dark_mode) {
        switch (type) {
        case NetlistTokenType::COMMENT:
            return slint::Color::from_rgb_uint8(0x8b, 0x94, 0x9e);
        case NetlistTokenType::DIRECTIVE:
            return slint::Color::from_rgb_uint8(0x79, 0xc0, 0xff);
        case NetlistTokenType::DEVICE:
            return slint::Color::from_rgb_uint8(0xff, 0xa6, 0x57);
        case NetlistTokenType::NODE:
            return foreground;
        case NetlistTokenType::NUMBER:
            return slint::Color::from_rgb_uint8(0xa5, 0xd6, 0xff);
        case NetlistTokenType::EXPRESSION:
            return slint::Color::from_rgb_uint8(0xff, 0x7b, 0x72);
        case NetlistTokenType::STRING_LITERAL:
            return slint::Color::from_rgb_uint8(0x7e, 0xe7, 0x87);
        case NetlistTokenType::CONTINUATION:
            return slint::Color::from_rgb_uint8(0xd2, 0xa8, 0xff);
        case NetlistTokenType::KEYWORD:
            return slint::Color::from_rgb_uint8(0xff, 0x88, 0x88);
        case NetlistTokenType::MODEL:
            return slint::Color::from_rgb_uint8(0xa5, 0xd6, 0xff);
        case NetlistTokenType::OPERATOR:
            return slint::Color::from_rgb_uint8(0xE0, 0xE0, 0xE0);
        case NetlistTokenType::WHITESPACE:
            return foreground;
        default:
            return foreground;
        }
    }
    // light theme
    switch (type) {
    case NetlistTokenType::COMMENT:
        return slint::Color::from_rgb_uint8(0x6a, 0x73, 0x7d);
    case NetlistTokenType::DIRECTIVE:
        return slint::Color::from_rgb_uint8(0x05, 0x50, 0xae);
    case NetlistTokenType::DEVICE:
        return slint::Color::from_rgb_uint8(0x95, 0x38, 0x00);
    case NetlistTokenType::NODE:
        return foreground;
    case NetlistTokenType::NUMBER:
        return slint::Color::from_rgb_uint8(0x0a, 0x30, 0x69);
    case NetlistTokenType::EXPRESSION:
        return slint::Color::from_rgb_uint8(0xcf, 0x22, 0x2e);
    case NetlistTokenType::STRING_LITERAL:
        return slint::Color::from_rgb_uint8(0x11, 0x63, 0x29);
    case NetlistTokenType::CONTINUATION:
        return slint::Color::from_rgb_uint8(0x82, 0x50, 0xdf);
    case NetlistTokenType::KEYWORD:
        return slint::Color::from_rgb_uint8(0xCC, 0x22, 0x22);
    case NetlistTokenType::MODEL:
        return slint::Color::from_rgb_uint8(0x0a, 0x30, 0x69);
    case NetlistTokenType::OPERATOR:
        return slint::Color::from_rgb_uint8(0x22, 0x22, 0x22);
    case NetlistTokenType::WHITESPACE:
        return foreground;
    default:
        return foreground;
    }
}

// count UTF-8 code points in a token; the token layer places glyphs on the
// TextInput's character grid, which advances one cell per code point, not per
// byte (e.g. an en dash in a comment is 3 bytes but one column)
[[nodiscard]] inline int utf8_code_point_count(std::string_view s) {
    // count bytes that are not UTF-8 continuation bytes (10xxxxxx)
    int count = 0;
    for (const unsigned char c : s)
        count += (c & 0xC0) != 0x80 ? 1 : 0;
    return count;
}

// convert a single tokenised line into the Slint model row consumed by
// NetlistEditor; the row carries its 1-based line number and a fresh token model
[[nodiscard]] inline main_window::HighlightedLine build_netlist_line_model(const NetlistTokenLine& line, int line_number, bool dark_mode, slint::Color foreground) {
    // per-line token model consumed by the row repeater
    auto tokens = std::make_shared<slint::VectorModel<main_window::HighlightedToken>>();
    // running code-point column of the next token
    int column_offset = 0;
    for (const auto& token : line.m_tokens) {
        main_window::HighlightedToken out_token;
        out_token.text = slint::SharedString(token.m_text);
        out_token.color = token_type_to_color(token.m_type, dark_mode, foreground);
        // remember where the token starts so the UI can place it on the
        // TextInput's fractional character grid
        out_token.column_offset = column_offset;
        column_offset += utf8_code_point_count(token.m_text);
        tokens->push_back(out_token);
    }
    main_window::HighlightedLine out_line;
    out_line.line_number = line_number;
    out_line.tokens = tokens;
    return out_line;
}

// test whether two tokenised lines carry identical token text and types;
// used to skip unchanged rows when updating the highlight model incrementally
[[nodiscard]] inline bool netlist_line_tokens_equal(const NetlistTokenLine& a, const NetlistTokenLine& b) {
    // different token counts are never equal
    if (a.m_tokens.size() != b.m_tokens.size())
        return false;
    for (std::size_t i = 0; i < a.m_tokens.size(); ++i) {
        // compare raw text and semantic type
        if (a.m_tokens[i].m_text != b.m_tokens[i].m_text || a.m_tokens[i].m_type != b.m_tokens[i].m_type)
            return false;
    }
    return true;
}

// convert tokenised lines produced by tokenize_netlist() into the Slint model
// consumed by NetlistEditor.  called on every netlist load, user edit, and
// theme change; each line carries a 1-based number and its own token model.
[[nodiscard]] inline std::shared_ptr<slint::VectorModel<main_window::HighlightedLine>> build_netlist_highlight_model(const std::vector<NetlistTokenLine>& lines, bool dark_mode, slint::Color foreground) {
    // one HighlightedLine per tokenised line
    auto model = std::make_shared<slint::VectorModel<main_window::HighlightedLine>>();
    // 1-based line number for the gutter
    int line_number = 1;
    for (const auto& line : lines) {
        // build the row through the shared per-line helper
        model->push_back(build_netlist_line_model(line, line_number++, dark_mode, foreground));
    }
    return model;
}
