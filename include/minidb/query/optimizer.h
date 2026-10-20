#ifndef MINIDB_QUERY_OPTIMIZER_H
#define MINIDB_QUERY_OPTIMIZER_H

#include "minidb/catalog/catalog.h"
#include "minidb/common/result.h"
#include "minidb/query/plan.h"
#include "minidb/storage/table_storage.h"

#include <memory>

namespace minidb::query {

class RuleBasedOptimizer {
public:
    RuleBasedOptimizer(const Catalog& catalog, const ITableStorage& storage)
        : catalog_(catalog), storage_(storage) {}

    [[nodiscard]] Result<std::unique_ptr<PlanNode>> optimize(
        std::unique_ptr<PlanNode> plan) const;

private:
    [[nodiscard]] Result<std::unique_ptr<PlanNode>> optimize_node(
        std::unique_ptr<PlanNode> node) const;

    const Catalog& catalog_;
    const ITableStorage& storage_;
};

} // namespace minidb::query

#endif // MINIDB_QUERY_OPTIMIZER_H
