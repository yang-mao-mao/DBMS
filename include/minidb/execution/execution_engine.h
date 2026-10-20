#ifndef MINIDB_EXECUTION_EXECUTION_ENGINE_H
#define MINIDB_EXECUTION_EXECUTION_ENGINE_H

#include "minidb/common/result.h"
#include "minidb/execution/operator.h"
#include "minidb/query/plan.h"
#include "minidb/storage/row.h"
#include "minidb/storage/table_storage.h"

#include <memory>
#include <vector>

namespace minidb::execution {

class ExecutionEngine {
public:
    explicit ExecutionEngine(const ITableStorage& storage) : storage_(storage) {}

    [[nodiscard]] Result<std::vector<Row>> execute(
        const query::PlanNode& plan) const;

private:
    [[nodiscard]] std::unique_ptr<Operator> build_operator(
        const query::PlanNode& plan) const;

    const ITableStorage& storage_;
};

} // namespace minidb::execution

#endif // MINIDB_EXECUTION_EXECUTION_ENGINE_H
