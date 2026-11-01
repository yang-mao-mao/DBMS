#ifndef MINIDB_SQL_PARSER_H
#define MINIDB_SQL_PARSER_H

#include "minidb/common/result.h"
#include "minidb/sql/ast.h"
#include "minidb/sql/token.h"

#include <span>
#include <string>
#include <string_view>

namespace minidb::sql {

// 针对 MiniDB SQL 子集的递归下降 Parser。
// Parser 只构造 AST，不访问 Catalog，也不执行数据库操作。
class Parser {
public:
    explicit Parser(std::span<const Token> tokens) : tokens_(tokens) {}

    // 解析一条完整语句，并拒绝分号之后的多余 Token。
    [[nodiscard]] Result<Statement> parse_statement();

private:
    [[nodiscard]] const Token& current() const noexcept;
    [[nodiscard]] const Token& previous() const noexcept;
    [[nodiscard]] bool check(TokenKind kind) const noexcept;
    bool match(TokenKind kind) noexcept;
    const Token& consume(TokenKind kind, std::string_view expectation);

    [[nodiscard]] CreateTableStatement parse_create_table();
    [[nodiscard]] CreateIndexStatement parse_create_index();
    [[nodiscard]] InsertStatement parse_insert();
    [[nodiscard]] SelectStatement parse_select();
    [[nodiscard]] DeleteStatement parse_delete();
    [[nodiscard]] ExplainStatement parse_explain();
    [[nodiscard]] ColumnDefinition parse_column_definition();
    [[nodiscard]] Value parse_literal();
    [[nodiscard]] Predicate parse_predicate();

    std::span<const Token> tokens_;
    std::size_t current_index_{0};
};

// 便捷入口：依次执行 Lexer 和 Parser。
[[nodiscard]] Result<Statement> parse_sql(std::string_view sql_text);

} // namespace minidb::sql

#endif // MINIDB_SQL_PARSER_H
