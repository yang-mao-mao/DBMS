#ifndef MINIDB_EXECUTION_OPERATOR_H
#define MINIDB_EXECUTION_OPERATOR_H

#include "minidb/common/result.h"
#include "minidb/query/plan.h"
#include "minidb/storage/table_storage.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace minidb::execution {

// Volcano/Pull 模型的统一算子协议：open 初始化，next 每次拉取一行，
// optional 为空表示正常结束，close 释放游标等运行期资源。
class Operator {
public:
    virtual ~Operator() = default;

    [[nodiscard]] virtual Status open() = 0;
    [[nodiscard]] virtual Result<std::optional<RowRecord>> next() = 0;
    [[nodiscard]] virtual Status close() = 0;
};

// 使用 ITableStorage::scan_rows 顺序拉取整张表。
class TableScanOperator final : public Operator {
public:
    TableScanOperator(const ITableStorage& storage, TableId table_id)
        : storage_(storage), table_id_(table_id) {}

    [[nodiscard]] Status open() override;
    [[nodiscard]] Result<std::optional<RowRecord>> next() override;
    [[nodiscard]] Status close() override;

private:
    const ITableStorage& storage_;
    TableId table_id_;
    std::unique_ptr<RowCursor> cursor_;
    bool open_{false};
};

// 先通过真实索引得到 RID 列表，再逐个按 RID 读取完整记录。
class IndexScanOperator final : public Operator {
public:
    IndexScanOperator(const ITableStorage& storage, TableId table_id,
                      std::string index_name, Value key)
        : storage_(storage),
          table_id_(table_id),
          index_name_(std::move(index_name)),
          key_(std::move(key)) {}

    [[nodiscard]] Status open() override;
    [[nodiscard]] Result<std::optional<RowRecord>> next() override;
    [[nodiscard]] Status close() override;

private:
    const ITableStorage& storage_;
    TableId table_id_;
    std::string index_name_;
    Value key_;
    std::vector<RID> rids_;
    std::size_t position_{0};
    bool open_{false};
};

// 不缓存全部结果：不断从子算子拉取，直到找到满足谓词的记录。
class FilterOperator final : public Operator {
public:
    FilterOperator(query::BoundPredicate predicate,
                   std::unique_ptr<Operator> child)
        : predicate_(std::move(predicate)), child_(std::move(child)) {}

    [[nodiscard]] Status open() override;
    [[nodiscard]] Result<std::optional<RowRecord>> next() override;
    [[nodiscard]] Status close() override;

private:
    query::BoundPredicate predicate_;
    std::unique_ptr<Operator> child_;
    bool open_{false};
};

// 对子算子返回的 Row 执行字段投影，并保留原记录 RID。
class ProjectOperator final : public Operator {
public:
    ProjectOperator(std::vector<std::uint32_t> column_indices,
                    std::unique_ptr<Operator> child)
        : column_indices_(std::move(column_indices)),
          child_(std::move(child)) {}

    [[nodiscard]] Status open() override;
    [[nodiscard]] Result<std::optional<RowRecord>> next() override;
    [[nodiscard]] Status close() override;

private:
    std::vector<std::uint32_t> column_indices_;
    std::unique_ptr<Operator> child_;
    bool open_{false};
};

} // namespace minidb::execution

#endif // MINIDB_EXECUTION_OPERATOR_H
