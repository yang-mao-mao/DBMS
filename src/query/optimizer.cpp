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
