#include "minidb/query/planner.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <variant>

namespace minidb::query {
namespace {

Result<std::uint32_t> find_column(const TableSchema& schema,
                                  std::string_view name) {
    const auto found = std::ranges::find(schema.columns, name,
                                         &ColumnDefinition::name);
    if (found == schema.columns.end()) {
        return Status::not_found("column does not exist: " + std::string(name));
    }
    const auto index = std::distance(schema.columns.begin(), found);
    if (index < 0
        || static_cast<std::uint64_t>(index)
               > std::numeric_limits<std::uint32_t>::max()) {
        return Status::internal_error("column index is outside UINT32 range");
    }
    return static_cast<std::uint32_t>(index);
}

bool value_matches_type(const Value& value, DataType type) {
    switch (type) {
        case DataType::Int64:
            return std::holds_alternative<std::int64_t>(value);
        case DataType::Varchar:
            return std::holds_alternative<std::string>(value);
    }
    return false;
}

} // namespace

Result<std::unique_ptr<PlanNode>> QueryPlanner::plan(
    const sql::SelectStatement& statement) const {
    auto table_result = catalog_.find_table(statement.table_name);
    if (!table_result.ok()) return table_result.status();
    TableMetadata table = std::move(table_result).value();

    std::unique_ptr<PlanNode> root =
        std::make_unique<TableScanPlan>(table);

    if (statement.where) {
        auto index_result = find_column(table.schema, statement.where->column);
        if (!index_result.ok()) return index_result.status();
        const std::uint32_t column_index = index_result.value();
        const auto& column = table.schema.columns[column_index];
        if (!value_matches_type(statement.where->value, column.type)) {
            return Status::invalid_argument(
                "WHERE value type does not match column: " + column.name);
        }
        BoundPredicate predicate{
            column.name,
            column_index,
            column.type,
            statement.where->operation,
            statement.where->value,
        };
        root = std::make_unique<FilterPlan>(std::move(predicate),
                                            std::move(root));
    }

    std::vector<std::uint32_t> projection_indices;
    std::vector<ColumnDefinition> projection_columns;
    if (std::holds_alternative<sql::AllColumns>(statement.projection)) {
        projection_indices.reserve(table.schema.columns.size());
        projection_columns = table.schema.columns;
        for (std::size_t i = 0; i < table.schema.columns.size(); ++i) {
            projection_indices.push_back(static_cast<std::uint32_t>(i));
        }
    } else {
        const auto& names =
            std::get<sql::NamedColumns>(statement.projection).names;
        projection_indices.reserve(names.size());
        projection_columns.reserve(names.size());
        for (const auto& name : names) {
            auto index_result = find_column(table.schema, name);
            if (!index_result.ok()) return index_result.status();
            projection_indices.push_back(index_result.value());
            projection_columns.push_back(
                table.schema.columns[index_result.value()]);
        }
    }

    root = std::make_unique<ProjectPlan>(std::move(projection_indices),
                                         std::move(projection_columns),
                                         std::move(root));
    return root;
}

} // namespace minidb::query
