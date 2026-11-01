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

// 计划节点既描述逻辑算子，也作为执行引擎构造物理算子的输入。
enum class PlanNodeKind {
    TableScan,
    IndexScan,
    Filter,
    Project,
};

// 经 Planner 校验和绑定后的谓词；执行阶段无需再次按名称查找字段。
struct BoundPredicate {
    std::string column_name;
    std::uint32_t column_index{0};
    DataType column_type{DataType::Int64};
    sql::ComparisonOperator operation{sql::ComparisonOperator::Equal};
    Value value;
};

// 所有计划节点的基类。计划树由 unique_ptr 独占，父节点拥有子节点。
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

// 顺序读取表中的全部记录。
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

// 根据等值键取得候选 RID；只有真实物理索引可用时优化器才会生成。
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

// 对子节点输出逐行计算单个 WHERE 谓词。
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

// 按字段下标重排或裁剪子节点输出。
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

// 生成供 EXPLAIN 和测试使用的可读树形计划。
[[nodiscard]] std::string format_plan(const PlanNode& plan);

} // namespace minidb::query

#endif // MINIDB_QUERY_PLAN_H
