#ifndef MINIDB_QUERY_PLANNER_H
#define MINIDB_QUERY_PLANNER_H

#include "minidb/catalog/catalog.h"
#include "minidb/common/result.h"
#include "minidb/query/plan.h"
#include "minidb/sql/ast.h"

#include <memory>

namespace minidb::query {

class QueryPlanner {
public:
    explicit QueryPlanner(const Catalog& catalog) : catalog_(catalog) {}

    [[nodiscard]] Result<std::unique_ptr<PlanNode>> plan(
        const sql::SelectStatement& statement) const;

private:
    const Catalog& catalog_;
};

} // namespace minidb::query

#endif // MINIDB_QUERY_PLANNER_H
