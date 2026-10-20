#ifndef MINIDB_SQL_TOKEN_H
#define MINIDB_SQL_TOKEN_H

#include <cstddef>
#include <string>

namespace minidb::sql {

enum class TokenKind {
    End,
    Identifier,
    IntegerLiteral,
    StringLiteral,

    Create,
    Table,
    Insert,
    Into,
    Values,
    Select,
    From,
    Where,
    Index,
    On,
    Explain,
    Int,
    Varchar,

    LeftParen,
    RightParen,
    Comma,
    Semicolon,
    Star,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

struct SourcePosition {
    std::size_t offset{0};
    std::size_t line{1};
    std::size_t column{1};

    bool operator==(const SourcePosition&) const = default;
};

struct Token {
    TokenKind kind{TokenKind::End};
    std::string text;
    SourcePosition position;

    bool operator==(const Token&) const = default;
};

[[nodiscard]] const char* token_kind_name(TokenKind kind) noexcept;

} // namespace minidb::sql

#endif // MINIDB_SQL_TOKEN_H
