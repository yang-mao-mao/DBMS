#include "minidb/execution/execution_engine.h"

#include <utility>

namespace minidb::execution {
namespace {

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
    auto operation = build_operator(plan);
    if (!operation) {
        return Status::internal_error("cannot build operator for query plan");
    }
    const Status opened = operation->open();
    if (!opened.ok()) return opened;
    OperatorGuard guard(*operation);

    std::vector<Row> rows;
    while (true) {
        auto next = operation->next();
        if (!next.ok()) return next.status();
        if (!next.value()) break;
        auto record = std::move(next).value();
        rows.push_back(std::move(record->row));
    }

    const Status closed = operation->close();
    guard.dismiss();
    if (!closed.ok()) return closed;
    return rows;
}

} // namespace minidb::execution
