#include "minidb/execution/execution_engine.h"

#include <utility>

namespace minidb::execution {
namespace {

// RAII 兜底：执行中途返回错误时也会关闭已经打开的根算子。
class OperatorGuard {
public:
    explicit OperatorGuard(Operator& operation) : operation_(operation) {}
    ~OperatorGuard() {
        if (active_) (void)operation_.close();
    }
    OperatorGuard(const OperatorGuard&) = delete;
    OperatorGuard& operator=(const OperatorGuard&) = delete;

    void dismiss() noexcept { active_ = false; }

private:
    Operator& operation_;
    bool active_{true};
};

} // namespace

std::unique_ptr<Operator> ExecutionEngine::build_operator(
    const query::PlanNode& plan) const {
    // 计划树与算子树保持同构，每个父算子独占其子算子。
    switch (plan.kind()) {
        case query::PlanNodeKind::TableScan: {
            const auto& scan = static_cast<const query::TableScanPlan&>(plan);
            return std::make_unique<TableScanOperator>(storage_, scan.table().id);
        }
        case query::PlanNodeKind::IndexScan: {
            const auto& scan = static_cast<const query::IndexScanPlan&>(plan);
            return std::make_unique<IndexScanOperator>(
                storage_, scan.table().id, scan.index_name(), scan.key());
        }
        case query::PlanNodeKind::Filter: {
            const auto& filter = static_cast<const query::FilterPlan&>(plan);
            return std::make_unique<FilterOperator>(
                filter.predicate(), build_operator(filter.child()));
        }
        case query::PlanNodeKind::Project: {
            const auto& project = static_cast<const query::ProjectPlan&>(plan);
            return std::make_unique<ProjectOperator>(
                project.column_indices(), build_operator(project.child()));
        }
    }
    return nullptr;
}

Result<std::vector<Row>> ExecutionEngine::execute(
    const query::PlanNode& plan) const {
    auto records = execute_records(plan);
    if (!records.ok()) return records.status();
    std::vector<Row> rows;
    rows.reserve(records.value().size());
    for (auto& record : records.value()) {
        rows.push_back(std::move(record.row));
    }
    return rows;
}

Result<std::vector<RowRecord>> ExecutionEngine::execute_records(
    const query::PlanNode& plan) const {
    // 驱动 Volcano 根算子，直到 next() 返回空 optional。
    auto operation = build_operator(plan);
    if (!operation) {
        return Status::internal_error("cannot build operator for query plan");
    }
    const Status opened = operation->open();
    if (!opened.ok()) return opened;
    OperatorGuard guard(*operation);

    std::vector<RowRecord> rows;
    while (true) {
        auto next = operation->next();
        if (!next.ok()) return next.status();
        if (!next.value()) break;
        rows.push_back(std::move(*next.value()));
    }

    const Status closed = operation->close();
    guard.dismiss();
    if (!closed.ok()) return closed;
    return rows;
}

} // namespace minidb::execution
