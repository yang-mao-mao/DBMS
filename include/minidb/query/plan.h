#ifndef MINIDB_QUERY_PLAN_H
#define MINIDB_QUERY_PLAN_H

#include "minidb/catalog/metadata.h"
#include "minidb/sql/ast.h"
#include "minidb/storage/row.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace minidb::query {

enum class PlanNodeKind {
    TableScan,
    IndexScan,
    Filter,
    Project,
};

struct BoundPredicate {
    std::string column_name;
    std::uint32_t column_index{0};
    DataType column_type{DataType::Int64};
    sql::ComparisonOperator operation{sql::ComparisonOperator::Equal};
    Value value;
};

class PlanNode {
public:
    virtual ~PlanNode() = default;

    [[nodiscard]] virtual PlanNodeKind kind() const noexcept = 0;
    [[nodiscard]] const std::vector<ColumnDefinition>& output_columns() const noexcept {
        return output_columns_;
    }

protected:
    explicit PlanNode(std::vector<ColumnDefinition> output_columns)
        : output_columns_(std::move(output_columns)) {}

private:
    std::vector<ColumnDefinition> output_columns_;
};

class TableScanPlan final : public PlanNode {
public:
    explicit TableScanPlan(TableMetadata table);

    [[nodiscard]] PlanNodeKind kind() const noexcept override {
        return PlanNodeKind::TableScan;
    }
    [[nodiscard]] const TableMetadata& table() const noexcept { return table_; }

private:
    TableMetadata table_;
};

class IndexScanPlan final : public PlanNode {
public:
    IndexScanPlan(TableMetadata table, std::string index_name, Value key);

    [[nodiscard]] PlanNodeKind kind() const noexcept override {
        return PlanNodeKind::IndexScan;
    }
    [[nodiscard]] const TableMetadata& table() const noexcept { return table_; }
    [[nodiscard]] const std::string& index_name() const noexcept {
        return index_name_;
    }
    [[nodiscard]] const Value& key() const noexcept { return key_; }

private:
    TableMetadata table_;
    std::string index_name_;
    Value key_;
};

class FilterPlan final : public PlanNode {
public:
    FilterPlan(BoundPredicate predicate, std::unique_ptr<PlanNode> child);

    [[nodiscard]] PlanNodeKind kind() const noexcept override {
        return PlanNodeKind::Filter;
    }
    [[nodiscard]] const BoundPredicate& predicate() const noexcept {
        return predicate_;
    }
    [[nodiscard]] const PlanNode& child() const noexcept { return *child_; }
    [[nodiscard]] PlanNode& child() noexcept { return *child_; }
    [[nodiscard]] std::unique_ptr<PlanNode> take_child() noexcept {
        return std::move(child_);
    }
    void set_child(std::unique_ptr<PlanNode> child) noexcept {
        child_ = std::move(child);
    }

private:
    BoundPredicate predicate_;
    std::unique_ptr<PlanNode> child_;
};

class ProjectPlan final : public PlanNode {
public:
    ProjectPlan(std::vector<std::uint32_t> column_indices,
                std::vector<ColumnDefinition> output_columns,
                std::unique_ptr<PlanNode> child);

    [[nodiscard]] PlanNodeKind kind() const noexcept override {
        return PlanNodeKind::Project;
    }
    [[nodiscard]] const std::vector<std::uint32_t>& column_indices() const noexcept {
        return column_indices_;
    }
    [[nodiscard]] const PlanNode& child() const noexcept { return *child_; }
    [[nodiscard]] PlanNode& child() noexcept { return *child_; }
    [[nodiscard]] std::unique_ptr<PlanNode> take_child() noexcept {
        return std::move(child_);
    }
    void set_child(std::unique_ptr<PlanNode> child) noexcept {
        child_ = std::move(child);
    }

private:
    std::vector<std::uint32_t> column_indices_;
    std::unique_ptr<PlanNode> child_;
};

[[nodiscard]] std::string format_plan(const PlanNode& plan);

} // namespace minidb::query

#endif // MINIDB_QUERY_PLAN_H
