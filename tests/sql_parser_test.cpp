#include "minidb/sql/parser.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <variant>

namespace {

using namespace minidb;
using namespace minidb::sql;

Statement parse_success(std::string_view text) {
    auto result = parse_sql(text);
    if (!result.ok()) std::cerr << result.status().message() << '\n';
    assert(result.ok());
    return std::move(result).value();
}

void expect_parse_failure(std::string_view text, std::string_view message_part) {
    auto result = parse_sql(text);
    assert(!result.ok());
    assert(result.status().code() == ErrorCode::InvalidArgument);
    assert(result.status().message().find(message_part) != std::string::npos);
    assert(result.status().message().find("line ") != std::string::npos);
}

void test_create_table() {
    auto statement = parse_success(
        "CREATE TABLE students (\n"
        " id INT, name VARCHAR(32), age INTEGER\n"
        ");");
    const auto& create = std::get<CreateTableStatement>(statement);
    assert(create.schema.name == "students");
    assert(create.schema.columns.size() == 3);
    assert((create.schema.columns[0]
            == ColumnDefinition{"id", DataType::Int64, 0, false}));
    assert((create.schema.columns[1]
            == ColumnDefinition{"name", DataType::Varchar, 32, false}));
    assert(create.schema.columns[2].type == DataType::Int64);

    std::cout << "CreateTable{name=students, columns=[id:Int64, "
                 "name:Varchar(32), age:Int64]}\n";
}

void test_insert_and_escaped_string() {
    auto statement = parse_success(
        "iNsErT iNtO students VaLuEs (1, 'Alice''s record; active', 20);");
    const auto& insert = std::get<InsertStatement>(statement);
    assert(insert.table_name == "students");
    assert(insert.values.size() == 3);
    assert(std::get<std::int64_t>(insert.values[0]) == 1);
    assert(std::get<std::string>(insert.values[1])
           == "Alice's record; active");
    assert(std::get<std::int64_t>(insert.values[2]) == 20);

    std::cout << "Insert{table=students, values=[1, "
                 "\"Alice's record; active\", 20]}\n";
}

void test_select_forms() {
    auto all_statement = parse_success("SELECT * FROM students;");
    const auto& select_all = std::get<SelectStatement>(all_statement);
    assert(std::holds_alternative<AllColumns>(select_all.projection));
    assert(!select_all.where.has_value());

    auto filtered_statement =
        parse_success("SELECT name FROM students WHERE age > 20;");
    const auto& filtered = std::get<SelectStatement>(filtered_statement);
    const auto& names = std::get<NamedColumns>(filtered.projection).names;
    assert(names == std::vector<std::string>{"name"});
    assert(filtered.where.has_value());
    assert(filtered.where->column == "age");
    assert(filtered.where->operation == ComparisonOperator::Greater);
    assert(std::get<std::int64_t>(filtered.where->value) == 20);

    std::cout << "Select{columns=[name], from=students, "
                 "where=Greater(age, 20)}\n";

    auto multi_column = parse_success("SELECT id, name FROM students");
    const auto& multi = std::get<SelectStatement>(multi_column);
    assert(std::get<NamedColumns>(multi.projection).names.size() == 2);
}

void test_create_index() {
    auto statement = parse_success("CREATE INDEX idx_id ON students(id);");
    const auto& create = std::get<CreateIndexStatement>(statement);
    assert(create.index_name == "idx_id");
    assert(create.table_name == "students");
    assert(create.column_name == "id");
}

void test_explain() {
    auto statement =
        parse_success("EXPLAIN SELECT name FROM students WHERE id = 1;");
    const auto& explain = std::get<ExplainStatement>(statement);
    assert(explain.query != nullptr);
    assert(explain.query->table_name == "students");
    assert(explain.query->where->operation == ComparisonOperator::Equal);

    std::cout << "Explain{Select{columns=[name], from=students, "
                 "where=Equal(id, 1)}}\n";
}

void test_invalid_syntax() {
    expect_parse_failure("CREATE TABLE t (id INT;", "expected ')'");
    expect_parse_failure("CREATE TABLE t ();", "at least one column");
    expect_parse_failure("SELECT FROM students;", "selected column");
    expect_parse_failure("INSERT INTO t VALUES ();", "at least one value");
    expect_parse_failure("DELETE FROM students;", "unsupported");
    expect_parse_failure("SELECT * FROM students garbage;", "end of input");
    expect_parse_failure("EXPLAIN INSERT INTO t VALUES (1);",
                         "SELECT after EXPLAIN");
}

void test_invalid_varchar_length() {
    expect_parse_failure("CREATE TABLE t (name VARCHAR(0));",
                         "VARCHAR length must be");
    expect_parse_failure("CREATE TABLE t (name VARCHAR(-1));",
                         "VARCHAR length must be");
    expect_parse_failure("CREATE TABLE t (name VARCHAR(4294967296));",
                         "VARCHAR length must be");
    expect_parse_failure(
        "CREATE TABLE t (name VARCHAR(999999999999999999999999));",
        "INT64 range");
}

} // namespace

int main() {
    test_create_table();
    test_insert_and_escaped_string();
    test_select_forms();
    test_create_index();
    test_explain();
    test_invalid_syntax();
    test_invalid_varchar_length();
}
