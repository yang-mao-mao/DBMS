#ifndef MINIDB_QUERY_OPTIMIZER_H
#define MINIDB_QUERY_OPTIMIZER_H

#include "minidb/catalog/catalog.h"
#include "minidb/common/result.h"
#include "minidb/query/plan.h"
#include "minidb/storage/table_storage.h"

#include <memory>

namespace minidb::query {

// 小型规则优化器。目前核心规则是：等值谓词且物理 B+ 树索引真实可用时，
// 将 Filter(TableScan) 的扫描子节点替换成 IndexScan；范围谓词保持全表扫描。
class RuleBasedOptimizer {
public:
    RuleBasedOptimizer(const Catalog& catalog, const ITableStorage& storage)
        : catalog_(catalog), storage_(storage) {}

    [[nodiscard]] Result<std::unique_ptr<PlanNode>> optimize(
        std::unique_ptr<PlanNode> plan) const;

private:
    // 递归优化子树，并保持原计划树的独占所有权。
    [[nodiscard]] Result<std::unique_ptr<PlanNode>> optimize_node(
        std::unique_ptr<PlanNode> node) const;

    const Catalog& catalog_;
    const ITableStorage& storage_;
};

} // namespace minidb::query

#endif // MINIDB_QUERY_OPTIMIZER_H
