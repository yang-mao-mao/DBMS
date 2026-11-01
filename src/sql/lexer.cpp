#include "minidb/sql/lexer.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace minidb::sql {
namespace {

bool is_identifier_start(char character) {
    const auto value = static_cast<unsigned char>(character);
    return std::isalpha(value) != 0 || character == '_';
}

bool is_identifier_continue(char character) {
    const auto value = static_cast<unsigned char>(character);
    return std::isalnum(value) != 0 || character == '_';
}

std::string uppercase(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char character) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    });
    return result;
}

Status lexical_error(const SourcePosition& position, std::string message) {
    std::ostringstream output;
    output << "lexical error at line " << position.line << ", column "
           << position.column << " (offset " << position.offset << "): "
           << message;
    return Status::invalid_argument(output.str());
}

const std::unordered_map<std::string, TokenKind> kKeywords{
    {"CREATE", TokenKind::Create},   {"TABLE", TokenKind::Table},
    {"INSERT", TokenKind::Insert},   {"INTO", TokenKind::Into},
    {"VALUES", TokenKind::Values},   {"SELECT", TokenKind::Select},
    {"FROM", TokenKind::From},       {"WHERE", TokenKind::Where},
    {"INDEX", TokenKind::Index},     {"ON", TokenKind::On},
    {"EXPLAIN", TokenKind::Explain}, {"INT", TokenKind::Int},
    {"INTEGER", TokenKind::Int},     {"VARCHAR", TokenKind::Varchar},
};

} // namespace

const char* token_kind_name(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::End: return "end of input";
        case TokenKind::Identifier: return "identifier";
        case TokenKind::IntegerLiteral: return "integer literal";
        case TokenKind::StringLiteral: return "string literal";
        case TokenKind::Create: return "CREATE";
        case TokenKind::Table: return "TABLE";
        case TokenKind::Insert: return "INSERT";
        case TokenKind::Into: return "INTO";
        case TokenKind::Values: return "VALUES";
        case TokenKind::Select: return "SELECT";
        case TokenKind::From: return "FROM";
        case TokenKind::Where: return "WHERE";
        case TokenKind::Index: return "INDEX";
        case TokenKind::On: return "ON";
        case TokenKind::Explain: return "EXPLAIN";
        case TokenKind::Int: return "INT";
        case TokenKind::Varchar: return "VARCHAR";
        case TokenKind::LeftParen: return "(";
        case TokenKind::RightParen: return ")";
        case TokenKind::Comma: return ",";
        case TokenKind::Semicolon: return ";";
        case TokenKind::Star: return "*";
        case TokenKind::Equal: return "=";
        case TokenKind::NotEqual: return "!=";
        case TokenKind::Less: return "<";
        case TokenKind::LessEqual: return "<=";
        case TokenKind::Greater: return ">";
        case TokenKind::GreaterEqual: return ">=";
    }
    return "unknown token";
}

bool Lexer::at_end() const noexcept { return offset_ >= input_.size(); }

char Lexer::peek(std::size_t lookahead) const noexcept {
    const std::size_t index = offset_ + lookahead;
    return index < input_.size() ? input_[index] : '\0';
}

char Lexer::advance() noexcept {
    const char character = input_[offset_++];
    if (character == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return character;
}

void Lexer::skip_whitespace() noexcept {
    while (!at_end()
           && std::isspace(static_cast<unsigned char>(peek())) != 0) {
        advance();
    }
}

SourcePosition Lexer::position() const noexcept {
    return {offset_, line_, column_};
}

Result<std::vector<Token>> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skip_whitespace();
        const SourcePosition start = position();
        if (at_end()) {
            tokens.push_back({TokenKind::End, {}, start});
            return tokens;
        }

        const char character = peek();
        if (is_identifier_start(character)) {
            const std::size_t begin = offset_;
            advance();
            while (is_identifier_continue(peek())) advance();
            std::string text(input_.substr(begin, offset_ - begin));
            const auto keyword = kKeywords.find(uppercase(text));
            tokens.push_back(
                {keyword == kKeywords.end() ? TokenKind::Identifier
                                             : keyword->second,
                 std::move(text), start});
            continue;
        }

        const bool is_negative_integer = character == '-'
            && std::isdigit(static_cast<unsigned char>(peek(1))) != 0;
        if (std::isdigit(static_cast<unsigned char>(character)) != 0
            || is_negative_integer) {
            const std::size_t begin = offset_;
            if (is_negative_integer) advance();
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                advance();
            }
            tokens.push_back({TokenKind::IntegerLiteral,
                              std::string(input_.substr(begin, offset_ - begin)),
                              start});
            continue;
        }

        if (character == '\'') {
            advance();
            std::string value;
            bool terminated = false;
            while (!at_end()) {
                const char next = advance();
                if (next != '\'') {
                    value.push_back(next);
                    continue;
                }
                if (peek() == '\'') {
                    advance();
                    value.push_back('\'');
                    continue;
                }
                terminated = true;
                break;
            }
            if (!terminated) {
                return lexical_error(start, "unterminated string literal");
            }
            tokens.push_back({TokenKind::StringLiteral, std::move(value), start});
            continue;
        }

        advance();
        switch (character) {
            case '(': tokens.push_back({TokenKind::LeftParen, "(", start}); break;
            case ')': tokens.push_back({TokenKind::RightParen, ")", start}); break;
            case ',': tokens.push_back({TokenKind::Comma, ",", start}); break;
            case ';': tokens.push_back({TokenKind::Semicolon, ";", start}); break;
            case '*': tokens.push_back({TokenKind::Star, "*", start}); break;
            case '=': tokens.push_back({TokenKind::Equal, "=", start}); break;
            case '!':
                if (peek() != '=') {
                    return lexical_error(start, "expected '=' after '!'");
                }
                advance();
                tokens.push_back({TokenKind::NotEqual, "!=", start});
                break;
            case '<':
                if (peek() == '=') {
                    advance();
                    tokens.push_back({TokenKind::LessEqual, "<=", start});
                } else if (peek() == '>') {
                    advance();
                    tokens.push_back({TokenKind::NotEqual, "<>", start});
                } else {
                    tokens.push_back({TokenKind::Less, "<", start});
                }
                break;
            case '>':
                if (peek() == '=') {
                    advance();
                    tokens.push_back({TokenKind::GreaterEqual, ">=", start});
                } else {
                    tokens.push_back({TokenKind::Greater, ">", start});
                }
                break;
            default:
                return lexical_error(start,
                                     std::string("illegal character '")
                                         + character + "'");
        }
    }
}

} // namespace minidb::sql
