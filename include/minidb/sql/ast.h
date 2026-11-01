#ifndef MINIDB_SQL_AST_H
#define MINIDB_SQL_AST_H

#include "minidb/catalog/metadata.h"
#include "minidb/storage/row.h"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace minidb::sql {

// WHERE 单条件支持的比较运算符。
enum class ComparisonOperator {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

// Parser 产生的未绑定谓词，字段名将在 Planner 阶段解析成字段下标。
struct Predicate {
    std::string column;
    ComparisonOperator operation{ComparisonOperator::Equal};
    Value value;

    bool operator==(const Predicate&) const = default;
};

struct AllColumns {
    bool operator==(const AllColumns&) const = default;
};

struct NamedColumns {
    std::vector<std::string> names;

    bool operator==(const NamedColumns&) const = default;
};

// SELECT * 与显式字段列表互斥，用 variant 保证类型安全。
using Projection = std::variant<AllColumns, NamedColumns>;

struct CreateTableStatement {
    TableSchema schema;

    bool operator==(const CreateTableStatement&) const = default;
};

struct InsertStatement {
    std::string table_name;
    Row values;

    bool operator==(const InsertStatement&) const = default;
};

struct SelectStatement {
    Projection projection;
    std::string table_name;
    std::optional<Predicate> where;

    bool operator==(const SelectStatement&) const = default;
};

struct DeleteStatement {
    std::string table_name;
    std::optional<Predicate> where;

    bool operator==(const DeleteStatement&) const = default;
};

struct CreateIndexStatement {
    std::string index_name;
    std::string table_name;
    std::string column_name;

    bool operator==(const CreateIndexStatement&) const = default;
};

// 当前 EXPLAIN 仅允许包裹 SELECT，unique_ptr 表达 AST 的独占所有权。
struct ExplainStatement {
    std::unique_ptr<SelectStatement> query;
};

// parse_sql 的统一输出；执行入口再按具体语句类型分派。
using Statement = std::variant<CreateTableStatement,
                               InsertStatement,
                               SelectStatement,
                               DeleteStatement,
                               CreateIndexStatement,
                               ExplainStatement>;

} // namespace minidb::sql

#endif // MINIDB_SQL_AST_H
