#include "minidb/query/plan.h"

#include <sstream>
#include <utility>

namespace minidb::query {
namespace {

std::string value_text(const Value& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*integer);
    }
    if (const auto* string = std::get_if<std::string>(&value)) {
        return "'" + *string + "'";
    }
    return "NULL";
}

const char* comparison_text(sql::ComparisonOperator operation) {
    switch (operation) {
        case sql::ComparisonOperator::Equal: return "=";
        case sql::ComparisonOperator::NotEqual: return "!=";
        case sql::ComparisonOperator::Less: return "<";
        case sql::ComparisonOperator::LessEqual: return "<=";
        case sql::ComparisonOperator::Greater: return ">";
        case sql::ComparisonOperator::GreaterEqual: return ">=";
    }
    return "?";
}

void append_plan(std::ostringstream& output, const PlanNode& plan,
                 std::size_t depth) {
    output << std::string(depth * 2, ' ');
    switch (plan.kind()) {
        case PlanNodeKind::TableScan: {
            const auto& scan = static_cast<const TableScanPlan&>(plan);
            output << "TableScan(table=" << scan.table().schema.name << ')';
            break;
        }
        case PlanNodeKind::IndexScan: {
            const auto& scan = static_cast<const IndexScanPlan&>(plan);
            output << "IndexScan(table=" << scan.table().schema.name
                   << ", index=" << scan.index_name()
                   << ", key=" << value_text(scan.key()) << ')';
            break;
        }
        case PlanNodeKind::Filter: {
            const auto& filter = static_cast<const FilterPlan&>(plan);
            const auto& predicate = filter.predicate();
            output << "Filter(" << predicate.column_name << ' '
                   << comparison_text(predicate.operation) << ' '
                   << value_text(predicate.value) << ")\n";
            append_plan(output, filter.child(), depth + 1);
            break;
        }
        case PlanNodeKind::Project: {
            const auto& project = static_cast<const ProjectPlan&>(plan);
            output << "Project(";
            for (std::size_t i = 0; i < project.output_columns().size(); ++i) {
                if (i != 0) output << ", ";
                output << project.output_columns()[i].name;
            }
            output << ")\n";
            append_plan(output, project.child(), depth + 1);
            break;
        }
    }
}

} // namespace

TableScanPlan::TableScanPlan(TableMetadata table)
    : PlanNode(table.schema.columns), table_(std::move(table)) {}

IndexScanPlan::IndexScanPlan(TableMetadata table, std::string index_name,
                             Value key)
    : PlanNode(table.schema.columns),
      table_(std::move(table)),
      index_name_(std::move(index_name)),
      key_(std::move(key)) {}

FilterPlan::FilterPlan(BoundPredicate predicate,
                       std::unique_ptr<PlanNode> child)
    : PlanNode(child->output_columns()),
      predicate_(std::move(predicate)),
      child_(std::move(child)) {}

ProjectPlan::ProjectPlan(std::vector<std::uint32_t> column_indices,
                         std::vector<ColumnDefinition> output_columns,
                         std::unique_ptr<PlanNode> child)
    : PlanNode(std::move(output_columns)),
      column_indices_(std::move(column_indices)),
      child_(std::move(child)) {}

std::string format_plan(const PlanNode& plan) {
    std::ostringstream output;
    append_plan(output, plan, 0);
    return output.str();
}

} // namespace minidb::query
