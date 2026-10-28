#pragma once

#include <string>
#include <string_view>
#include <vector>

// semantic token types for syntax highlighting of Xyce netlists
enum class NetlistTokenType
{
    // comment line starting with '*' or ';', or trailing inline comment
    COMMENT,
    // simulation or circuit directive starting with '.'
    DIRECTIVE,
    // component or device identifier (e.g. R1, C_filt, V1, X1, YMEMRISTOR)
    DEVICE,
    // circuit net or node identifier (e.g. 0, GND, IN, OUT, N1)
    NODE,
    // numeric literal or engineering quantity (e.g. 10k, 100n, 1uF, 20m, 1Meg)
    NUMBER,
    // mathematical expression enclosed in curly braces ({...})
    EXPRESSION,
    // quoted string literal (e.g. "tran.raw")
    STRING_LITERAL,
    // line continuation indicator '+' at the start of a line
    CONTINUATION,
    // predefined keyword or function name (e.g. PULSE, SIN, TRAN, FORMAT)
    KEYWORD,
    // model or subcircuit reference name on a device instance line
    // (e.g. NPN_MOD after a transistor node list, LM317 after an X node list)
    MODEL,
    // punctuation and operator symbols (e.g. '=', '(', ')', '+', '-')
    OPERATOR,
    // whitespace sequence (spaces and tabs)
    WHITESPACE,
    // unclassified plain text
    PLAIN_TEXT,
};

// represents an individual lexical token within a line
struct NetlistToken
{
    // raw text of the token
    std::string m_text;
    // classified semantic type
    NetlistTokenType m_type;
};

// represents a tokenized line of a netlist
struct NetlistTokenLine
{
    // ordered sequence of tokens that form the line
    std::vector<NetlistToken> m_tokens;
    // line ending that terminated this line ("\n", "\r\n", or "\r");
    // empty for the final line when the input has no trailing newline
    std::string m_line_ending;
};

// tokenize a raw netlist string into tokenized lines preserving all whitespace
// and the line ending of every line
[[nodiscard]] std::vector<NetlistTokenLine> tokenize_netlist(std::string_view netlist);
