#include "minidb/sql/lexer.h"

#include <cassert>
#include <string>

namespace {

using minidb::ErrorCode;
using minidb::sql::Lexer;
using minidb::sql::TokenKind;

void test_keywords_are_case_insensitive() {
    Lexer lexer("SeLeCt name FrOm Students WhErE age >= 20;");
    auto result = lexer.tokenize();
    assert(result.ok());
    const auto& tokens = result.value();
    assert(tokens[0].kind == TokenKind::Select);
    assert(tokens[1].kind == TokenKind::Identifier);
    assert(tokens[1].text == "name");
    assert(tokens[2].kind == TokenKind::From);
    assert(tokens[3].text == "Students");
    assert(tokens[4].kind == TokenKind::Where);
    assert(tokens[6].kind == TokenKind::GreaterEqual);
    assert(tokens[7].kind == TokenKind::IntegerLiteral);
    assert(tokens[8].kind == TokenKind::Semicolon);
    assert(tokens[9].kind == TokenKind::End);
}

void test_strings_preserve_spaces_semicolons_and_escaped_quotes() {
    Lexer lexer("INSERT INTO notes VALUES ('Alice''s note; keep it');");
    auto result = lexer.tokenize();
    assert(result.ok());
    const auto& tokens = result.value();
    assert(tokens[5].kind == TokenKind::StringLiteral);
    assert(tokens[5].text == "Alice's note; keep it");
}

void test_whitespace_and_source_positions() {
    Lexer lexer("SELECT\n  *\nFROM students;");
    auto result = lexer.tokenize();
    assert(result.ok());
    const auto& tokens = result.value();
    assert(tokens[1].kind == TokenKind::Star);
    assert(tokens[1].position.line == 2);
    assert(tokens[1].position.column == 3);
    assert(tokens[2].kind == TokenKind::From);
    assert(tokens[2].position.line == 3);
    assert(tokens[2].position.column == 1);
}

void test_lexical_errors_report_positions() {
    Lexer illegal("SELECT @ FROM t;");
    auto result = illegal.tokenize();
    assert(!result.ok());
    assert(result.status().code() == ErrorCode::InvalidArgument);
    assert(result.status().message().find("line 1, column 8") != std::string::npos);
    assert(result.status().message().find("illegal character") != std::string::npos);

    Lexer unterminated("INSERT INTO t VALUES ('unfinished);");
    result = unterminated.tokenize();
    assert(!result.ok());
    assert(result.status().message().find("unterminated string literal")
           != std::string::npos);
}

void test_comparison_operators() {
    Lexer lexer("= != <> < <= > >=");
    auto result = lexer.tokenize();
    assert(result.ok());
    const auto& tokens = result.value();
    assert(tokens[0].kind == TokenKind::Equal);
    assert(tokens[1].kind == TokenKind::NotEqual);
    assert(tokens[2].kind == TokenKind::NotEqual);
    assert(tokens[3].kind == TokenKind::Less);
    assert(tokens[4].kind == TokenKind::LessEqual);
    assert(tokens[5].kind == TokenKind::Greater);
    assert(tokens[6].kind == TokenKind::GreaterEqual);
}

} // namespace

int main() {
    test_keywords_are_case_insensitive();
    test_strings_preserve_spaces_semicolons_and_escaped_quotes();
    test_whitespace_and_source_positions();
    test_lexical_errors_report_positions();
    test_comparison_operators();
}
