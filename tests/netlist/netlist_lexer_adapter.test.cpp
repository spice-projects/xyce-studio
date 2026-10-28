#include <gtest/gtest.h>

#include <chrono>

#include "netlist/netlist_lexer.h"
#include "ui/netlist_lexer_adapter.h"

// test foreground colour used for tokens that follow the editor text
static const slint::Color TEST_FOREGROUND = slint::Color::from_rgb_uint8(0x12, 0x34, 0x56);

// token_type_to_color: comment tokens use the light-mode spec colour
TEST(NetlistLexerAdapterChecks, comment_uses_spec_colour_light_mode) {
    // arrange / act
    const auto color = token_type_to_color(NetlistTokenType::COMMENT, false, TEST_FOREGROUND);
    // assert
    ASSERT_EQ(color, slint::Color::from_rgb_uint8(0x6a, 0x73, 0x7d));
}

// token_type_to_color: comment tokens use the dark-mode spec colour
TEST(NetlistLexerAdapterChecks, comment_uses_spec_colour_dark_mode) {
    // arrange / act
    const auto color = token_type_to_color(NetlistTokenType::COMMENT, true, TEST_FOREGROUND);
    // assert
    ASSERT_EQ(color, slint::Color::from_rgb_uint8(0x8b, 0x94, 0x9e));
}

// token_type_to_color: directive colour is distinct between themes
TEST(NetlistLexerAdapterChecks, directive_colour_differs_between_themes) {
    // arrange / act / assert
    ASSERT_NE(token_type_to_color(NetlistTokenType::DIRECTIVE, true, TEST_FOREGROUND), token_type_to_color(NetlistTokenType::DIRECTIVE, false, TEST_FOREGROUND));
}

// token_type_to_color: plain text colour differs from the directive colour in light mode
TEST(NetlistLexerAdapterChecks, directive_colour_differs_from_plain_text) {
    // arrange / act / assert
    ASSERT_NE(token_type_to_color(NetlistTokenType::DIRECTIVE, false, TEST_FOREGROUND), token_type_to_color(NetlistTokenType::PLAIN_TEXT, false, TEST_FOREGROUND));
}

// token_type_to_color: model names share the numeric colour so trailing value
// and model arguments (e.g. 20k on a resistor line, LM317 on a subcircuit line)
// render with the same colour
TEST(NetlistLexerAdapterChecks, model_colour_matches_number_colour_dark_mode) {
    // arrange / act / assert
    ASSERT_EQ(token_type_to_color(NetlistTokenType::MODEL, true, TEST_FOREGROUND), token_type_to_color(NetlistTokenType::NUMBER, true, TEST_FOREGROUND));
}

TEST(NetlistLexerAdapterChecks, model_colour_matches_number_colour_light_mode) {
    // arrange / act / assert
    ASSERT_EQ(token_type_to_color(NetlistTokenType::MODEL, false, TEST_FOREGROUND), token_type_to_color(NetlistTokenType::NUMBER, false, TEST_FOREGROUND));
}

// token_type_to_color: node tokens follow the theme foreground colour
TEST(NetlistLexerAdapterChecks, node_colour_follows_foreground) {
    // arrange / act / assert
    ASSERT_EQ(token_type_to_color(NetlistTokenType::NODE, true, TEST_FOREGROUND), TEST_FOREGROUND);
    ASSERT_EQ(token_type_to_color(NetlistTokenType::NODE, false, TEST_FOREGROUND), TEST_FOREGROUND);
}

// build_netlist_highlight_model: model row count equals tokenized line count
TEST(NetlistLexerAdapterChecks, model_row_count_matches_token_lines) {
    // arrange
    const std::string netlist = "* comment\nR1 1 0 1k";
    // act
    const auto token_lines = tokenize_netlist(netlist);
    const auto model = build_netlist_highlight_model(token_lines, false, TEST_FOREGROUND);
    // assert
    ASSERT_EQ(model->row_count(), token_lines.size());
}

// build_netlist_highlight_model: line numbers are 1-based and sequential
TEST(NetlistLexerAdapterChecks, line_numbers_are_sequential) {
    // arrange
    const std::string netlist = "* a\n* b\n* c";
    // act
    const auto token_lines = tokenize_netlist(netlist);
    const auto model = build_netlist_highlight_model(token_lines, false, TEST_FOREGROUND);
    // assert
    for (std::size_t i = 0; i < model->row_count(); ++i)
        ASSERT_EQ(model->row_data(i).value().line_number, static_cast<int>(i + 1));
}

// build_netlist_highlight_model: no token text is a bare newline
TEST(NetlistLexerAdapterChecks, bare_newline_tokens_are_dropped) {
    // arrange: multi-line input so every model row is inspected, including the
    // rows adjacent to the newline delimiters
    const std::string netlist = "* comment\nR1 1 0 1k\n";
    // act
    const auto model = build_netlist_highlight_model(tokenize_netlist(netlist), false, TEST_FOREGROUND);
    // assert: three rows for two content lines plus the trailing empty line
    ASSERT_EQ(model->row_count(), 3u);
    for (std::size_t row = 0; row < model->row_count(); ++row) {
        const auto line = model->row_data(row).value();
        for (std::size_t i = 0; i < line.tokens->row_count(); ++i)
            ASSERT_NE(std::string(line.tokens->row_data(i).value().text), "\n");
    }
}

// build_netlist_highlight_model: empty string produces exactly one line
TEST(NetlistLexerAdapterChecks, empty_netlist_produces_one_line) {
    // arrange
    const std::string netlist;
    // act
    const auto token_lines = tokenize_netlist(netlist);
    const auto model = build_netlist_highlight_model(token_lines, false, TEST_FOREGROUND);
    // assert
    ASSERT_EQ(model->row_count(), 1u);
}

// build_netlist_highlight_model: the leading token of a directive line carries
// the directive colour and its text
TEST(NetlistLexerAdapterChecks, directive_line_tokens_carry_directive_colour) {
    // arrange
    const std::string netlist = ".tran 1n 10u";
    // act
    const auto model = build_netlist_highlight_model(tokenize_netlist(netlist), false, TEST_FOREGROUND);
    // assert
    ASSERT_GE(model->row_count(), 1u);
    const auto line = model->row_data(0).value();
    ASSERT_GE(line.tokens->row_count(), 1u);
    const auto first = line.tokens->row_data(0).value();
    ASSERT_EQ(std::string(first.text), ".tran");
    ASSERT_EQ(first.color, token_type_to_color(NetlistTokenType::DIRECTIVE, false, TEST_FOREGROUND));
}

// build_netlist_highlight_model: node tokens carry the foreground colour
TEST(NetlistLexerAdapterChecks, node_tokens_carry_foreground_colour) {
    // arrange
    const std::string netlist = "R1 IN 0 1k";
    // act
    const auto model = build_netlist_highlight_model(tokenize_netlist(netlist), false, TEST_FOREGROUND);
    // assert
    ASSERT_GE(model->row_count(), 1u);
    const auto line = model->row_data(0).value();
    ASSERT_GE(line.tokens->row_count(), 3u);
    const auto node_token = line.tokens->row_data(2).value();
    ASSERT_EQ(std::string(node_token.text), "IN");
    ASSERT_EQ(node_token.color, TEST_FOREGROUND);
}

// build_netlist_highlight_model: tokens carry their code-point column so the
// UI can place them on the TextInput's character grid; multi-byte UTF-8
// characters (3 bytes) advance the column by one, not by their byte count
TEST(NetlistLexerAdapterChecks, token_columns_count_code_points_not_bytes) {
    // arrange: the expression token contains an en dash (U+2013, 3 bytes)
    const std::string netlist = ".TRAN 1u {x\u2013y}";
    // act
    const auto model = build_netlist_highlight_model(tokenize_netlist(netlist), false, TEST_FOREGROUND);
    // assert: tokens are ".TRAN", " ", "1u", " ", "{x-y}" at columns 0,5,6,8,9
    const auto line = model->row_data(0).value();
    ASSERT_EQ(line.tokens->row_count(), 5u);
    ASSERT_EQ(line.tokens->row_data(0).value().column_offset, 0);
    ASSERT_EQ(line.tokens->row_data(1).value().column_offset, 5);
    ASSERT_EQ(line.tokens->row_data(2).value().column_offset, 6);
    ASSERT_EQ(line.tokens->row_data(3).value().column_offset, 8);
    ASSERT_EQ(line.tokens->row_data(4).value().column_offset, 9);
}

// build_netlist_highlight_model: rebuilding the 1000-line highlight model runs
// on every editor keystroke, so its cost must stay negligible
TEST(NetlistLexerAdapterChecks, builds_thousand_line_model_within_budget) {
    // arrange: 1000 generated device lines mixing devices, KiCad net names,
    // numeric values and comments
    std::string netlist;
    netlist.reserve(1000 * 48);
    for (int i = 0; i < 1000; ++i) {
        // no trailing newline so the netlist is exactly 1000 lines
        if (i > 0)
            netlist += '\n';
        netlist += "R" + std::to_string(i) + " IN" + std::to_string(i) + " Net-_U303A-G" + std::to_string(i) + "_ " + std::to_string(i) + "k * transient";
    }
    // act
    const auto token_lines = tokenize_netlist(netlist);
    const auto start = std::chrono::steady_clock::now();
    const auto model = build_netlist_highlight_model(token_lines, false, TEST_FOREGROUND);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    // assert: structural correctness and a time budget for unoptimized debug
    // builds; the measurement is logged so drift stays visible
    ASSERT_EQ(model->row_count(), 1000u);
    ASSERT_LT(elapsed, 80000) << "build_netlist_highlight_model(1000 lines) took " << elapsed << "us";
    SUCCEED() << "build_netlist_highlight_model(1000 lines) took " << elapsed << "us";
}
