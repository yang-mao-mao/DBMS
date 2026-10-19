#ifndef MINIDB_STORAGE_TABLE_STORAGE_H
#define MINIDB_STORAGE_TABLE_STORAGE_H

#include "minidb/catalog/metadata.h"
#include "minidb/common/result.h"
#include "minidb/storage/row.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace minidb {

class RowCursor {
public:
    virtual ~RowCursor() = default;

    [[nodiscard]] virtual Result<std::optional<RowRecord>> next() = 0;
};

class ITableStorage {
public:
    virtual ~ITableStorage() = default;

    [[nodiscard]] virtual Result<TableId> create_table(
        std::string_view name,
        std::span<const ColumnDefinition> columns) = 0;

    [[nodiscard]] virtual Result<TableSchema> get_schema(TableId table_id) const = 0;

    [[nodiscard]] virtual Result<RID> insert_row(
        TableId table_id, const Row& row) = 0;

    [[nodiscard]] virtual Result<std::unique_ptr<RowCursor>> scan_rows(
        TableId table_id) const = 0;

    [[nodiscard]] virtual Result<std::optional<RowRecord>> get_row_by_rid(
        TableId table_id, RID rid) const = 0;

    [[nodiscard]] virtual Status create_index(
        TableId table_id, std::string_view name,
        std::span<const std::uint32_t> key_columns) = 0;

    [[nodiscard]] virtual Result<bool> has_index(
        TableId table_id, std::string_view name) const = 0;

    [[nodiscard]] virtual Result<std::vector<RID>> index_lookup(
        TableId table_id, std::string_view name, const Value& key) const = 0;
};

} // namespace minidb

#endif // MINIDB_STORAGE_TABLE_STORAGE_H
