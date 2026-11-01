#ifndef MINIDB_SQL_TOKEN_H
#define MINIDB_SQL_TOKEN_H

#include <cstddef>
#include <string>

namespace minidb::sql {

// 词法单元类型。关键字在 Lexer 中按大小写不敏感方式识别。
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
    Delete,
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

// Token 起始位置，用于生成精确到行、列的错误诊断。
struct SourcePosition {
    std::size_t offset{0};
    std::size_t line{1};
    std::size_t column{1};

    bool operator==(const SourcePosition&) const = default;
};

// text 保存原始词素（字符串字面量保存转义后的内容）。
struct Token {
    TokenKind kind{TokenKind::End};
    std::string text;
    SourcePosition position;

    bool operator==(const Token&) const = default;
};

[[nodiscard]] const char* token_kind_name(TokenKind kind) noexcept;

} // namespace minidb::sql

#endif // MINIDB_SQL_TOKEN_H
