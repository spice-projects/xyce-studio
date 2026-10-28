#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include "xyce_parser.h"

namespace
{
    const std::unordered_map<char, TokenKind> SINGLE_CHAR_TOKENS{
        {'(', TokenKind::LPAREN}, {')', TokenKind::RPAREN}, {'{', TokenKind::LBRACE}, {'}', TokenKind::RBRACE}, {',', TokenKind::COMMA}, {'?', TokenKind::QUESTION}, {':', TokenKind::COLON}, {'@', TokenKind::AT}, {'+', TokenKind::PLUS}, {'-', TokenKind::MINUS}, {'*', TokenKind::STAR}, {'/', TokenKind::SLASH}, {'%', TokenKind::PERCENT}, {'^', TokenKind::CARET}, {'~', TokenKind::TILDE}, {'!', TokenKind::BANG}, {'&', TokenKind::AMPERSAND}, {'|', TokenKind::PIPE}, {'<', TokenKind::LESS}, {'>', TokenKind::GREATER},
    };

    const std::unordered_map<std::string_view, TokenKind> DOUBLE_CHAR_TOKENS{
        {"&&", TokenKind::LOGICAL_AND}, {"||", TokenKind::LOGICAL_OR}, {"==", TokenKind::EQUAL_EQUAL}, {"!=", TokenKind::BANG_EQUAL}, {"<=", TokenKind::LESS_EQUAL}, {">=", TokenKind::GREATER_EQUAL}, {"**", TokenKind::POWER},
    };

    bool is_identifier_start(const char ch) {
        // identifiers start with a letter or an underscore
        return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
    }

    bool is_identifier_part(const char ch) {
        // identifiers continue with letters, digits, underscores, or square brackets
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '[' || ch == ']';
    }

    std::string token_kind_name(const TokenKind kind) {
        // return the token name used in parser error messages
        switch (kind) {
        case TokenKind::DIRECTIVE:
            return "DIRECTIVE";
        case TokenKind::IDENTIFIER:
            return "IDENTIFIER";
        case TokenKind::NUMBER:
            return "NUMBER";
        case TokenKind::LPAREN:
            return "LPAREN";
        case TokenKind::RPAREN:
            return "RPAREN";
        case TokenKind::LBRACE:
            return "LBRACE";
        case TokenKind::RBRACE:
            return "RBRACE";
        case TokenKind::COMMA:
            return "COMMA";
        case TokenKind::QUESTION:
            return "QUESTION";
        case TokenKind::COLON:
            return "COLON";
        case TokenKind::AT:
            return "AT";
        case TokenKind::PLUS:
            return "PLUS";
        case TokenKind::MINUS:
            return "MINUS";
        case TokenKind::STAR:
            return "STAR";
        case TokenKind::SLASH:
            return "SLASH";
        case TokenKind::PERCENT:
            return "PERCENT";
        case TokenKind::CARET:
            return "CARET";
        case TokenKind::TILDE:
            return "TILDE";
        case TokenKind::BANG:
            return "BANG";
        case TokenKind::AMPERSAND:
            return "AMPERSAND";
        case TokenKind::PIPE:
            return "PIPE";
        case TokenKind::LOGICAL_AND:
            return "LOGICAL_AND";
        case TokenKind::LOGICAL_OR:
            return "LOGICAL_OR";
        case TokenKind::EQUAL_EQUAL:
            return "EQUAL_EQUAL";
        case TokenKind::BANG_EQUAL:
            return "BANG_EQUAL";
        case TokenKind::LESS:
            return "LESS";
        case TokenKind::LESS_EQUAL:
            return "LESS_EQUAL";
        case TokenKind::GREATER:
            return "GREATER";
        case TokenKind::GREATER_EQUAL:
            return "GREATER_EQUAL";
        case TokenKind::POWER:
            return "POWER";
        case TokenKind::EOF_TOKEN:
            return "EOF";
        }
        return "UNKNOWN";
    }

    void append_token(std::vector<Token>& tokens, const TokenKind kind, const std::string& text, const size_t start, const size_t end) {
        // append a scanned token
        tokens.push_back(Token{kind, text, start, end});
    }

    std::vector<Token> tokenize_impl(const std::string& text) {
        // collect output tokens
        std::vector<Token> tokens;
        // scan the input buffer from left to right
        for (size_t pos = 0; pos < text.size();) {
            // read the current character
            const char ch = text[pos];
            // skip whitespace
            if (std::isspace(static_cast<unsigned char>(ch))) {
                ++pos;
                continue;
            }
            // scan a directive token
            if (ch == '.') {
                // remember the token start position
                const size_t start = pos;
                // require an identifier head after the dot
                if (pos + 1 >= text.size() || !is_identifier_start(text[pos + 1])) {
                    throw std::invalid_argument("Unexpected character '.' at offset " + std::to_string(pos));
                }
                // consume the directive head
                pos += 2;
                // consume the directive tail
                while (pos < text.size() && is_identifier_part(text[pos])) {
                    ++pos;
                }
                // append the directive token
                append_token(tokens, TokenKind::DIRECTIVE, text.substr(start, pos - start), start, pos);
                continue;
            }
            // scan a two-character operator token
            if (pos + 1 < text.size()) {
                const std::string_view candidate(text.data() + pos, 2);
                if (const auto it = DOUBLE_CHAR_TOKENS.find(candidate); it != DOUBLE_CHAR_TOKENS.end()) {
                    append_token(tokens, it->second, std::string(candidate), pos, pos + 2);
                    pos += 2;
                    continue;
                }
            }
            // scan a number literal
            if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.') {
                // remember the token start position
                const size_t start = pos;
                // track whether at least one digit was consumed
                bool saw_digit = false;
                // handle a leading decimal point
                if (ch == '.') {
                    if (pos + 1 >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos + 1]))) {
                        throw std::invalid_argument("Unexpected character '.' at offset " + std::to_string(pos));
                    }
                    ++pos;
                }
                // consume the integer part
                while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                    ++pos;
                    saw_digit = true;
                }
                // consume the fractional part
                if (pos < text.size() && text[pos] == '.') {
                    ++pos;
                    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                        ++pos;
                        saw_digit = true;
                    }
                }
                // reject a token that never consumed a digit
                if (!saw_digit) {
                    throw std::invalid_argument("Unexpected character '.' at offset " + std::to_string(start));
                }
                // consume an exponent when present
                if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
                    // compute the first exponent digit position
                    size_t exp_pos = pos + 1;
                    // consume an optional exponent sign
                    if (exp_pos < text.size() && (text[exp_pos] == '+' || text[exp_pos] == '-')) {
                        ++exp_pos;
                    }
                    // remember the start of the exponent digits
                    const size_t exp_start = exp_pos;
                    // consume exponent digits
                    while (exp_pos < text.size() && std::isdigit(static_cast<unsigned char>(text[exp_pos]))) {
                        ++exp_pos;
                    }
                    // reject exponents with no digits
                    if (exp_pos == exp_start) {
                        throw std::invalid_argument("Invalid exponent at offset " + std::to_string(pos));
                    }
                    // commit the exponent scan
                    pos = exp_pos;
                }
                // append the scanned number
                append_token(tokens, TokenKind::NUMBER, text.substr(start, pos - start), start, pos);
                continue;
            }
            // scan an identifier token
            if (is_identifier_start(ch)) {
                // remember the token start position
                const size_t start = pos;
                // consume the identifier head
                ++pos;
                // consume the identifier tail
                while (pos < text.size() && is_identifier_part(text[pos])) {
                    ++pos;
                }
                // append the identifier token
                append_token(tokens, TokenKind::IDENTIFIER, text.substr(start, pos - start), start, pos);
                continue;
            }
            // scan a single-character token
            if (const auto it = SINGLE_CHAR_TOKENS.find(ch); it != SINGLE_CHAR_TOKENS.end()) {
                append_token(tokens, it->second, std::string(1, ch), pos, pos + 1);
                ++pos;
                continue;
            }
            // fail on unsupported input
            throw std::invalid_argument("Unexpected character '" + std::string(1, ch) + "' at offset " + std::to_string(pos));
        }
        // append the end-of-input token
        tokens.push_back(Token{TokenKind::EOF_TOKEN, "", text.size(), text.size()});
        return tokens;
    }

    std::string lowercase(std::string text) {
        // fold the string to lowercase
        std::ranges::transform(text, text.begin(), [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        // return the folded string
        return text;
    }
} // namespace

ExpressionPtr XyceParser::parse_expression(const std::string& text) {
    // tokenize the input
    m_tokens = tokenize(text);
    // reset the cursor
    m_pos = 0;
    // parse the expression
    auto expression = parse_ternary();
    // require end of input
    consume(TokenKind::EOF_TOKEN);
    // return the parsed tree
    return expression;
}

FunctionDefinitionNode XyceParser::parse_function_definition(const std::string& text) {
    // tokenize the input
    m_tokens = tokenize(text);
    // reset the cursor
    m_pos = 0;
    // parse the directive token
    const auto directive = consume(TokenKind::DIRECTIVE);
    // require a .func directive
    if (lowercase(directive.text) != ".func") {
        throw std::invalid_argument("Expected .func directive at offset " + std::to_string(directive.start));
    }
    // parse the function name
    const auto name = consume(TokenKind::IDENTIFIER).text;
    // consume the parameter list opener
    consume(TokenKind::LPAREN);
    // parse the parameter list
    const auto params = parse_parameter_list();
    // consume the parameter list closer
    consume(TokenKind::RPAREN);
    // consume the body opener
    consume(TokenKind::LBRACE);
    // parse the body
    auto body = parse_ternary();
    // consume the body closer
    consume(TokenKind::RBRACE);
    // require end of input
    consume(TokenKind::EOF_TOKEN);
    // return the function definition
    return {name, params, std::move(body)};
}

std::vector<std::string> XyceParser::parse_parameter_list() {
    // collect parameter names
    std::vector<std::string> params;
    // exit early for an empty list
    if (peek().kind == TokenKind::RPAREN) {
        return params;
    }
    // parse comma-separated parameters
    while (true) {
        // append the next parameter
        params.push_back(consume(TokenKind::IDENTIFIER).text);
        // exit when the list ends
        if (peek().kind != TokenKind::COMMA) {
            return params;
        }
        // consume the separator
        consume(TokenKind::COMMA);
    }
}

ExpressionPtr XyceParser::parse_ternary() {
    // parse the condition
    auto condition = parse_logical_or();
    // exit when there is no ternary operator
    if (peek().kind != TokenKind::QUESTION) {
        return condition;
    }
    // consume the ternary marker
    consume(TokenKind::QUESTION);
    // parse the true branch
    auto if_true = parse_ternary();
    // consume the branch separator
    consume(TokenKind::COLON);
    // parse the false branch
    auto if_false = parse_ternary();
    // return the ternary node
    return std::make_unique<TernaryOperationNode>(std::move(condition), std::move(if_true), std::move(if_false));
}

ExpressionPtr XyceParser::parse_logical_or() {
    // parse the left-hand side
    auto expression = parse_logical_xor();
    // fold chained logical-or operators
    while (peek().kind == TokenKind::LOGICAL_OR || peek().kind == TokenKind::PIPE) {
        // consume the operator token
        consume(peek().kind);
        // fold the right-hand side
        expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::LOGICAL_OR, parse_logical_xor());
    }
    // return the folded expression
    return expression;
}

ExpressionPtr XyceParser::parse_logical_xor() {
    // parse the left-hand side
    auto expression = parse_logical_and();
    // fold chained logical-xor operators
    while (peek().kind == TokenKind::CARET) {
        // consume the operator token
        consume(TokenKind::CARET);
        // fold the right-hand side
        expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::LOGICAL_XOR, parse_logical_and());
    }
    // return the folded expression
    return expression;
}

ExpressionPtr XyceParser::parse_logical_and() {
    // parse the left-hand side
    auto expression = parse_equality();
    // fold chained logical-and operators
    while (peek().kind == TokenKind::LOGICAL_AND || peek().kind == TokenKind::AMPERSAND) {
        // consume the operator token
        consume(peek().kind);
        // fold the right-hand side
        expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::LOGICAL_AND, parse_equality());
    }
    // return the folded expression
    return expression;
}

ExpressionPtr XyceParser::parse_equality() {
    // parse the left-hand side
    auto expression = parse_relational();
    // fold chained equality operators
    while (peek().kind == TokenKind::EQUAL_EQUAL || peek().kind == TokenKind::BANG_EQUAL) {
        // consume the operator token
        const auto kind = consume(peek().kind).kind;
        // fold equality or inequality
        if (kind == TokenKind::EQUAL_EQUAL) {
            expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::EQUAL, parse_relational());
        }
        else {
            expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::NOT_EQUAL, parse_relational());
        }
    }
    // return the folded expression
    return expression;
}

ExpressionPtr XyceParser::parse_relational() {
    // parse the left-hand side
    auto expression = parse_additive();
    // fold chained relational operators
    while (peek().kind == TokenKind::LESS || peek().kind == TokenKind::LESS_EQUAL || peek().kind == TokenKind::GREATER || peek().kind == TokenKind::GREATER_EQUAL) {
        // consume the operator token
        const auto kind = consume(peek().kind).kind;
        // fold the relational comparison
        if (kind == TokenKind::LESS) {
            expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::LESS, parse_additive());
        }
        else if (kind == TokenKind::LESS_EQUAL) {
            expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::LESS_EQUAL, parse_additive());
        }
        else if (kind == TokenKind::GREATER) {
            expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::GREATER, parse_additive());
        }
        else {
            expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::GREATER_EQUAL, parse_additive());
        }
    }
    // return the folded expression
    return expression;
}

ExpressionPtr XyceParser::parse_additive() {
    // parse the left-hand side
    auto expression = parse_multiplicative();
    // fold chained additive operators
    while (peek().kind == TokenKind::PLUS || peek().kind == TokenKind::MINUS) {
        // consume the operator token
        const auto kind = consume(peek().kind).kind;
        // fold addition or subtraction
        if (kind == TokenKind::PLUS) {
            expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::ADD, parse_multiplicative());
        }
        else {
            expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::SUB, parse_multiplicative());
        }
    }
    // return the folded expression
    return expression;
}

bool XyceParser::last_was_primary(const ExpressionNode* node) {
    // return true for a simple primary
    if (dynamic_cast<const NumberNode*>(node) != nullptr || dynamic_cast<const IdentifierNode*>(node) != nullptr) {
        return true;
    }
    // recurse through binary expressions so chains like "a * 1s" still work
    if (const auto* binary = dynamic_cast<const BinaryOperationNode*>(node); binary != nullptr) {
        return last_was_primary(binary->right.get());
    }
    // otherwise there is no trailing primary
    return false;
}

ExpressionPtr XyceParser::parse_multiplicative() {
    // parse the left-hand side
    auto expression = parse_unary();
    // fold explicit and implicit multiplicative operators
    while (true) {
        // handle explicit operators first
        if (peek().kind == TokenKind::STAR || peek().kind == TokenKind::SLASH || peek().kind == TokenKind::PERCENT) {
            // consume the operator token
            const auto kind = consume(peek().kind).kind;
            // parse the right-hand side
            auto right = parse_unary();
            // fold the operator
            if (kind == TokenKind::STAR) {
                expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::MUL, std::move(right));
            }
            else if (kind == TokenKind::SLASH) {
                expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::DIV, std::move(right));
            }
            else {
                expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::MOD, std::move(right));
            }
            continue;
        }
        // handle implicit suffix multiplication like 1K or 10MEG
        if (peek().kind == TokenKind::IDENTIFIER && last_was_primary(expression.get())) {
            // consume the suffix token and treat it as the right-hand side
            const auto right_token = consume(TokenKind::IDENTIFIER);
            // fold the suffix as a multiplication
            expression = std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::MUL, std::make_unique<IdentifierNode>(right_token.text));
            continue;
        }
        // no more multiplicative operators
        break;
    }
    // return the folded expression
    return expression;
}

ExpressionPtr XyceParser::parse_unary() {
    // parse unary plus
    if (peek().kind == TokenKind::PLUS) {
        consume(TokenKind::PLUS);
        return std::make_unique<UnaryOperationNode>(UnaryOperator::POS, parse_unary());
    }
    // parse unary minus
    if (peek().kind == TokenKind::MINUS) {
        consume(TokenKind::MINUS);
        return std::make_unique<UnaryOperationNode>(UnaryOperator::NEG, parse_unary());
    }
    // parse logical negation
    if (peek().kind == TokenKind::TILDE || peek().kind == TokenKind::BANG) {
        consume(peek().kind);
        return std::make_unique<UnaryOperationNode>(UnaryOperator::NOT, parse_unary());
    }
    // fall through to power
    return parse_power();
}

ExpressionPtr XyceParser::parse_power() {
    // parse the base expression
    auto expression = parse_primary();
    // fold a right-associative power operator
    if (peek().kind == TokenKind::POWER) {
        consume(TokenKind::POWER);
        return std::make_unique<BinaryOperationNode>(std::move(expression), BinaryOperator::POW, parse_unary());
    }
    // return the base expression
    return expression;
}

ExpressionPtr XyceParser::parse_primary() {
    // inspect the current token
    const auto token = peek();
    // parse a number literal
    if (token.kind == TokenKind::NUMBER) {
        return std::make_unique<NumberNode>(consume(TokenKind::NUMBER).text);
    }
    // parse an identifier or function call
    if (token.kind == TokenKind::IDENTIFIER) {
        // consume the identifier token
        const auto identifier = consume(TokenKind::IDENTIFIER).text;
        // exit early for a bare identifier
        if (peek().kind != TokenKind::LPAREN) {
            return parse_postfix_reference(std::make_unique<IdentifierNode>(identifier));
        }
        // consume the argument list opener
        consume(TokenKind::LPAREN);
        // parse the argument list
        std::vector<ExpressionPtr> args;
        const auto lowered = lowercase(identifier);
        if (lowered == "v" || lowered == "i") {
            args = parse_probe_argument_list();
        }
        else {
            args = parse_argument_list();
        }
        // consume the argument list closer
        consume(TokenKind::RPAREN);
        // parse an optional step selector
        return parse_postfix_reference(std::make_unique<FunctionCallNode>(identifier, std::move(args)));
    }
    // parse a parenthesized sub-expression
    if (token.kind == TokenKind::LPAREN) {
        consume(TokenKind::LPAREN);
        auto expression = parse_ternary();
        consume(TokenKind::RPAREN);
        return expression;
    }
    // fail on an unexpected token
    throw std::invalid_argument("Unexpected token '" + token.text + "' at offset " + std::to_string(token.start));
}

ExpressionPtr XyceParser::parse_postfix_reference(ExpressionPtr expression) {
    // exit early when there is no step selector
    if (peek().kind != TokenKind::AT) {
        return expression;
    }
    // consume the selector marker
    consume(TokenKind::AT);
    // inspect the selector payload
    const auto suffix = peek();
    // require a numeric selector
    if (suffix.kind != TokenKind::NUMBER) {
        throw std::invalid_argument("Expected numeric step index after @ at offset " + std::to_string(suffix.start));
    }
    // consume the numeric selector text
    const auto selector_text = consume(TokenKind::NUMBER).text;
    // parse the selector as an integer
    long long step_index = 0;
    try {
        size_t parsed = 0;
        step_index = std::stoll(selector_text, &parsed, 10);
        if (parsed != selector_text.size()) {
            throw std::invalid_argument("");
        }
    }
    catch (const std::exception&) {
        throw std::invalid_argument("Step selector @" + selector_text + " is not a valid integer");
    }
    // reject zero and negative selectors
    if (step_index < 1) {
        throw std::invalid_argument("Step selector @" + std::to_string(step_index) + " is invalid: indices must be >= 1");
    }
    // return the wrapped selector node
    return std::make_unique<StepSelectorNode>(std::move(expression), static_cast<size_t>(step_index));
}

std::vector<ExpressionPtr> XyceParser::parse_argument_list() {
    // collect argument expressions
    std::vector<ExpressionPtr> args;
    // exit early for an empty list
    if (peek().kind == TokenKind::RPAREN) {
        return args;
    }
    // parse comma-separated arguments
    while (true) {
        // append the next argument
        args.push_back(parse_ternary());
        // exit when the list ends
        if (peek().kind != TokenKind::COMMA) {
            return args;
        }
        // consume the separator
        consume(TokenKind::COMMA);
    }
}

std::vector<ExpressionPtr> XyceParser::parse_probe_argument_list() {
    // collect raw node-name arguments
    std::vector<ExpressionPtr> args;
    // exit early for an empty list
    if (peek().kind == TokenKind::RPAREN) {
        return args;
    }
    // parse comma-separated node names
    while (true) {
        // append the next node name
        args.push_back(parse_probe_node_name());
        // exit when the list ends
        if (peek().kind != TokenKind::COMMA) {
            return args;
        }
        // consume the separator
        consume(TokenKind::COMMA);
    }
}

ExpressionPtr XyceParser::parse_probe_node_name() {
    // collect raw node-name fragments
    std::vector<std::string> parts;
    // consume every token that can legally appear in a SPICE node name
    while (peek().kind == TokenKind::IDENTIFIER || peek().kind == TokenKind::NUMBER || peek().kind == TokenKind::MINUS || peek().kind == TokenKind::PLUS || peek().kind == TokenKind::SLASH || peek().kind == TokenKind::COLON) {
        parts.push_back(consume(peek().kind).text);
    }
    // require at least one fragment
    if (parts.empty()) {
        throw std::invalid_argument("Expected probe node name at offset " + std::to_string(peek().start));
    }
    // reconstruct the node name
    std::string name;
    for (const auto& part : parts) {
        name += part;
    }
    // wrap the node name as an identifier
    return std::make_unique<IdentifierNode>(std::move(name));
}

const Token& XyceParser::peek() const {
    // return the current token
    return m_tokens.at(m_pos);
}

const Token& XyceParser::consume(const TokenKind kind) {
    // read the current token
    const auto& token = peek();
    // fail on a kind mismatch
    if (token.kind != kind) {
        throw std::invalid_argument("Expected " + token_kind_name(kind) + " at offset " + std::to_string(token.start) + ", got " + token_kind_name(token.kind));
    }
    // advance the cursor
    ++m_pos;
    // return the consumed token
    return token;
}

ExpressionPtr parse_expression(const std::string& text) {
    // parse a standalone expression with a fresh parser instance
    return XyceParser{}.parse_expression(text);
}

std::vector<Token> XyceLexer::tokenize(const std::string& text) { return tokenize_impl(text); }

std::vector<Token> tokenize(const std::string& text) { return XyceLexer{}.tokenize(text); }

FunctionDefinitionNode parse_function_definition(const std::string& text) {
    // parse a .func definition with a fresh parser instance
    return XyceParser{}.parse_function_definition(text);
}
