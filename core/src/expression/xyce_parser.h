#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

enum class TokenKind
{
    DIRECTIVE,
    IDENTIFIER,
    NUMBER,
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    COMMA,
    QUESTION,
    COLON,
    AT,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,
    CARET,
    TILDE,
    BANG,
    AMPERSAND,
    PIPE,
    LOGICAL_AND,
    LOGICAL_OR,
    EQUAL_EQUAL,
    BANG_EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL,
    POWER,
    EOF_TOKEN
};

struct Token
{
    TokenKind kind;
    std::string text;
    size_t start;
    size_t end;
};

enum class BinaryOperator
{
    LOGICAL_OR,
    LOGICAL_XOR,
    LOGICAL_AND,
    EQUAL,
    NOT_EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL,
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    POW
};

enum class UnaryOperator
{
    POS,
    NEG,
    NOT
};

struct ExpressionNode
{
    virtual ~ExpressionNode() = default;
};

using ExpressionPtr = std::unique_ptr<ExpressionNode>;

struct NumberNode final : ExpressionNode
{
    explicit NumberNode(std::string value) :
        text(std::move(value)) {}

    std::string text;
};

struct IdentifierNode final : ExpressionNode
{
    explicit IdentifierNode(std::string value) :
        name(std::move(value)) {}

    std::string name;
};

struct FunctionCallNode final : ExpressionNode
{
    FunctionCallNode(std::string value, std::vector<ExpressionPtr> arguments) :
        name(std::move(value)), args(std::move(arguments)) {}

    std::string name;
    std::vector<ExpressionPtr> args;
};

struct UnaryOperationNode final : ExpressionNode
{
    UnaryOperationNode(UnaryOperator value, ExpressionPtr operand_value) :
        operator_value(value), operand(std::move(operand_value)) {}

    UnaryOperator operator_value;
    ExpressionPtr operand;
};

struct BinaryOperationNode final : ExpressionNode
{
    BinaryOperationNode(ExpressionPtr lhs, BinaryOperator value, ExpressionPtr rhs) :
        left(std::move(lhs)), operator_value(value), right(std::move(rhs)) {}

    ExpressionPtr left;
    BinaryOperator operator_value;
    ExpressionPtr right;
};

struct TernaryOperationNode final : ExpressionNode
{
    TernaryOperationNode(ExpressionPtr condition_value, ExpressionPtr true_value, ExpressionPtr false_value) :
        condition(std::move(condition_value)), if_true(std::move(true_value)), if_false(std::move(false_value)) {}

    ExpressionPtr condition;
    ExpressionPtr if_true;
    ExpressionPtr if_false;
};

struct StepSelectorNode final : ExpressionNode
{
    StepSelectorNode(ExpressionPtr value, size_t index) :
        base(std::move(value)), step_index(index) {}

    ExpressionPtr base;
    size_t step_index;
};

struct FunctionDefinitionNode
{
    FunctionDefinitionNode(std::string value, std::vector<std::string> parameters, ExpressionPtr value_body) :
        name(std::move(value)), params(std::move(parameters)), body(std::move(value_body)) {}

    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<ExpressionNode> body;
};

class XyceLexer
{
public:
    std::vector<Token> tokenize(const std::string& text);
};

class XyceParser
{
public:
    XyceParser() = default;

    XyceParser(const XyceParser&) = delete;

    XyceParser(XyceParser&&) noexcept = default;

    ~XyceParser() = default;

    XyceParser& operator=(const XyceParser&) = delete;

    XyceParser& operator=(XyceParser&&) noexcept = default;

    ExpressionPtr parse_expression(const std::string& text);

    FunctionDefinitionNode parse_function_definition(const std::string& text);

private:
    std::vector<Token> m_tokens;
    size_t m_pos = 0;

    std::vector<std::string> parse_parameter_list();
    ExpressionPtr parse_ternary();
    ExpressionPtr parse_logical_or();
    ExpressionPtr parse_logical_xor();
    ExpressionPtr parse_logical_and();
    ExpressionPtr parse_equality();
    ExpressionPtr parse_relational();
    ExpressionPtr parse_additive();
    ExpressionPtr parse_multiplicative();
    ExpressionPtr parse_unary();
    ExpressionPtr parse_power();
    ExpressionPtr parse_primary();
    ExpressionPtr parse_postfix_reference(ExpressionPtr expression);
    std::vector<ExpressionPtr> parse_argument_list();
    std::vector<ExpressionPtr> parse_probe_argument_list();
    ExpressionPtr parse_probe_node_name();

    [[nodiscard]] const Token& peek() const;
    const Token& consume(TokenKind kind);
    static bool last_was_primary(const ExpressionNode* node);
};

std::vector<Token> tokenize(const std::string& text);

ExpressionPtr parse_expression(const std::string& text);

FunctionDefinitionNode parse_function_definition(const std::string& text);
