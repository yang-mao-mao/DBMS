#ifndef MINIDB_QUERY_PLANNER_H
#define MINIDB_QUERY_PLANNER_H

#include "minidb/catalog/catalog.h"
#include "minidb/common/result.h"
#include "minidb/query/plan.h"
#include "minidb/sql/ast.h"

#include <memory>

namespace minidb::query {

// 将 SELECT AST 绑定到 Catalog 中的真实表和字段，并构造初始逻辑计划。
class QueryPlanner {
public:
    explicit QueryPlanner(const Catalog& catalog) : catalog_(catalog) {}

    [[nodiscard]] Result<std::unique_ptr<PlanNode>> plan(
        const sql::SelectStatement& statement) const;

private:
    // 非拥有引用；Database 保证 Catalog/Storage 生命周期长于 Planner。
    const Catalog& catalog_;
};

} // namespace minidb::query

#endif // MINIDB_QUERY_PLANNER_H
