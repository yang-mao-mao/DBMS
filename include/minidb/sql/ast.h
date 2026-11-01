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

enum class ComparisonOperator {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

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

struct CreateIndexStatement {
    std::string index_name;
    std::string table_name;
    std::string column_name;

    bool operator==(const CreateIndexStatement&) const = default;
};

struct ExplainStatement {
    std::unique_ptr<SelectStatement> query;
};

using Statement = std::variant<CreateTableStatement,
                               InsertStatement,
                               SelectStatement,
                               CreateIndexStatement,
                               ExplainStatement>;

} // namespace minidb::sql

#endif // MINIDB_SQL_AST_H
