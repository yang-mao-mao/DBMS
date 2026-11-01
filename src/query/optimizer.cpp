#include "minidb/query/optimizer.h"

#include <utility>

namespace minidb::query {

Result<std::unique_ptr<PlanNode>> RuleBasedOptimizer::optimize(
    std::unique_ptr<PlanNode> plan) const {
    if (!plan) return Status::invalid_argument("query plan cannot be null");
    return optimize_node(std::move(plan));
}

Result<std::unique_ptr<PlanNode>> RuleBasedOptimizer::optimize_node(
    std::unique_ptr<PlanNode> node) const {
    // 自顶向下识别节点，并先递归优化子树。
    if (node->kind() == PlanNodeKind::Project) {
        auto& project = static_cast<ProjectPlan&>(*node);
        auto child = optimize_node(project.take_child());
        if (!child.ok()) return child.status();
        project.set_child(std::move(child).value());
        return node;
    }

    if (node->kind() != PlanNodeKind::Filter) return node;

    auto& filter = static_cast<FilterPlan&>(*node);
    auto child = optimize_node(filter.take_child());
    if (!child.ok()) return child.status();
    filter.set_child(std::move(child).value());

    // 当前物理 B+ 树已接入等值查找；范围计划尚未建模，仍保留顺序扫描。
    if (filter.predicate().operation != sql::ComparisonOperator::Equal
        || filter.child().kind() != PlanNodeKind::TableScan) {
        return node;
    }

    const auto& scan = static_cast<const TableScanPlan&>(filter.child());
    for (const auto& index : catalog_.list_indexes(scan.table().id)) {
        if (index.state != IndexState::Available
            || index.key_columns.size() != 1
            || index.key_columns.front() != filter.predicate().column_index) {
            continue;
        }
        // Catalog 的 Available 只是元数据状态，还要由存储后端确认物理索引可用。
        auto available = storage_.has_index(scan.table().id, index.name);
        if (!available.ok()) return available.status();
        if (!available.value()) continue;

        filter.set_child(std::make_unique<IndexScanPlan>(
            scan.table(), index.name, filter.predicate().value));
        break;
    }
    return node;
}

} // namespace minidb::query
