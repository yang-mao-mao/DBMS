#include "minidb/sql/parser.h"

#include "minidb/sql/lexer.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace minidb::sql {
namespace {

class ParseFailure final : public std::runtime_error {
public:
    ParseFailure(const Token& token, std::string message)
        : std::runtime_error(format(token, std::move(message))) {}

private:
    static std::string format(const Token& token, std::string message) {
        std::ostringstream output;
        output << "syntax error at line " << token.position.line << ", column "
               << token.position.column << " (offset " << token.position.offset
               << "): " << message;
        return output.str();
    }
};

std::int64_t parse_int64(const Token& token) {
    std::int64_t value = 0;
    const char* begin = token.text.data();
    const char* end = begin + token.text.size();
    const auto [parsed_end, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || parsed_end != end) {
        throw ParseFailure(token, "integer literal is outside INT64 range");
    }
    return value;
}

} // namespace

const Token& Parser::current() const noexcept {
    return tokens_[current_index_ < tokens_.size() ? current_index_
                                                   : tokens_.size() - 1];
}

const Token& Parser::previous() const noexcept {
    return tokens_[current_index_ == 0 ? 0 : current_index_ - 1];
}

bool Parser::check(TokenKind kind) const noexcept {
    return current().kind == kind;
}

bool Parser::match(TokenKind kind) noexcept {
    if (!check(kind)) return false;
    ++current_index_;
    return true;
}

const Token& Parser::consume(TokenKind kind, std::string_view expectation) {
    if (!check(kind)) {
        throw ParseFailure(current(),
                           "expected " + std::string(expectation) + ", found "
                               + token_kind_name(current().kind));
    }
    ++current_index_;
    return previous();
}

Result<Statement> Parser::parse_statement() {
    if (tokens_.empty()) {
        return Status::invalid_argument("parser requires an End token");
    }

    try {
        Statement statement;
        if (match(TokenKind::Create)) {
            if (match(TokenKind::Table)) {
                statement = parse_create_table();
            } else if (match(TokenKind::Index)) {
                statement = parse_create_index();
            } else {
                throw ParseFailure(current(), "expected TABLE or INDEX after CREATE");
            }
        } else if (match(TokenKind::Insert)) {
            statement = parse_insert();
        } else if (match(TokenKind::Select)) {
            statement = parse_select();
        } else if (match(TokenKind::Explain)) {
            statement = parse_explain();
        } else {
            throw ParseFailure(current(), "unsupported or missing SQL statement");
        }

        match(TokenKind::Semicolon);
        consume(TokenKind::End, "end of input");
        return statement;
    } catch (const ParseFailure& error) {
        return Status::invalid_argument(error.what());
    }
}

CreateTableStatement Parser::parse_create_table() {
    TableSchema schema;
    schema.name = consume(TokenKind::Identifier, "table name").text;
    consume(TokenKind::LeftParen, "'('");
    if (check(TokenKind::RightParen)) {
        throw ParseFailure(current(), "CREATE TABLE requires at least one column");
    }
    schema.columns.push_back(parse_column_definition());
    while (match(TokenKind::Comma)) {
        schema.columns.push_back(parse_column_definition());
    }
    consume(TokenKind::RightParen, "')'");
    return {std::move(schema)};
}

ColumnDefinition Parser::parse_column_definition() {
    ColumnDefinition column;
    column.name = consume(TokenKind::Identifier, "column name").text;
    if (match(TokenKind::Int)) {
        column.type = DataType::Int64;
        column.max_length = 0;
    } else if (match(TokenKind::Varchar)) {
        column.type = DataType::Varchar;
        consume(TokenKind::LeftParen, "'(' after VARCHAR");
        const Token& length_token =
            consume(TokenKind::IntegerLiteral, "positive VARCHAR length");
        const std::int64_t length = parse_int64(length_token);
        if (length <= 0
            || static_cast<std::uint64_t>(length)
                   > std::numeric_limits<std::uint32_t>::max()) {
            throw ParseFailure(
                length_token,
                "VARCHAR length must be between 1 and UINT32_MAX");
        }
        column.max_length = static_cast<std::size_t>(length);
        consume(TokenKind::RightParen, "')' after VARCHAR length");
    } else {
        throw ParseFailure(current(), "expected INT or VARCHAR column type");
    }
    column.nullable = false;
    return column;
}

CreateIndexStatement Parser::parse_create_index() {
    CreateIndexStatement statement;
    statement.index_name = consume(TokenKind::Identifier, "index name").text;
    consume(TokenKind::On, "ON");
    statement.table_name = consume(TokenKind::Identifier, "table name").text;
    consume(TokenKind::LeftParen, "'('");
    statement.column_name = consume(TokenKind::Identifier, "indexed column name").text;
    consume(TokenKind::RightParen, "')'");
    return statement;
}

InsertStatement Parser::parse_insert() {
    InsertStatement statement;
    consume(TokenKind::Into, "INTO");
    statement.table_name = consume(TokenKind::Identifier, "table name").text;
    consume(TokenKind::Values, "VALUES");
    consume(TokenKind::LeftParen, "'('");
    if (check(TokenKind::RightParen)) {
        throw ParseFailure(current(), "VALUES requires at least one value");
    }
    statement.values.push_back(parse_literal());
    while (match(TokenKind::Comma)) {
        statement.values.push_back(parse_literal());
    }
    consume(TokenKind::RightParen, "')'");
    return statement;
}

SelectStatement Parser::parse_select() {
    SelectStatement statement;
    if (match(TokenKind::Star)) {
        statement.projection = AllColumns{};
    } else {
        NamedColumns columns;
        columns.names.push_back(consume(TokenKind::Identifier, "selected column").text);
        while (match(TokenKind::Comma)) {
            columns.names.push_back(
                consume(TokenKind::Identifier, "selected column").text);
        }
        statement.projection = std::move(columns);
    }

    consume(TokenKind::From, "FROM");
    statement.table_name = consume(TokenKind::Identifier, "table name").text;
    if (match(TokenKind::Where)) statement.where = parse_predicate();
    return statement;
}

ExplainStatement Parser::parse_explain() {
    consume(TokenKind::Select, "SELECT after EXPLAIN");
    return {std::make_unique<SelectStatement>(parse_select())};
}

Value Parser::parse_literal() {
    if (match(TokenKind::IntegerLiteral)) return parse_int64(previous());
    if (match(TokenKind::StringLiteral)) return previous().text;
    throw ParseFailure(current(), "expected integer or string literal");
}

Predicate Parser::parse_predicate() {
    Predicate predicate;
    predicate.column = consume(TokenKind::Identifier, "WHERE column").text;
    if (match(TokenKind::Equal)) {
        predicate.operation = ComparisonOperator::Equal;
    } else if (match(TokenKind::NotEqual)) {
        predicate.operation = ComparisonOperator::NotEqual;
    } else if (match(TokenKind::Less)) {
        predicate.operation = ComparisonOperator::Less;
    } else if (match(TokenKind::LessEqual)) {
        predicate.operation = ComparisonOperator::LessEqual;
    } else if (match(TokenKind::Greater)) {
        predicate.operation = ComparisonOperator::Greater;
    } else if (match(TokenKind::GreaterEqual)) {
        predicate.operation = ComparisonOperator::GreaterEqual;
    } else {
        throw ParseFailure(current(), "expected comparison operator");
    }
    predicate.value = parse_literal();
    return predicate;
}

Result<Statement> parse_sql(std::string_view sql_text) {
    Lexer lexer(sql_text);
    auto tokens = lexer.tokenize();
    if (!tokens.ok()) return tokens.status();
    Parser parser(tokens.value());
    return parser.parse_statement();
}

} // namespace minidb::sql
