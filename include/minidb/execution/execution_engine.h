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

// 将计划树递归翻译为算子树，并驱动根算子直到结果耗尽。
class ExecutionEngine {
public:
    explicit ExecutionEngine(const ITableStorage& storage) : storage_(storage) {}

    [[nodiscard]] Result<std::vector<Row>> execute(
        const query::PlanNode& plan) const;

    // DML 可保留 RID 消费计划结果；SELECT 的 execute() 只提取逻辑行。
    [[nodiscard]] Result<std::vector<RowRecord>> execute_records(
        const query::PlanNode& plan) const;

private:
    // 返回的根算子独占整个子算子树。
    [[nodiscard]] std::unique_ptr<Operator> build_operator(
        const query::PlanNode& plan) const;

    const ITableStorage& storage_;
};

} // namespace minidb::execution

#endif // MINIDB_EXECUTION_EXECUTION_ENGINE_H
