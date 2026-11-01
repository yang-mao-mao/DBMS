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

class Operator {
public:
    virtual ~Operator() = default;

    [[nodiscard]] virtual Status open() = 0;
    [[nodiscard]] virtual Result<std::optional<RowRecord>> next() = 0;
    [[nodiscard]] virtual Status close() = 0;
};

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
