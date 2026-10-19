#ifndef MINIDB_STORAGE_REAL_STORAGE_ADAPTER_H
#define MINIDB_STORAGE_REAL_STORAGE_ADAPTER_H

#include "minidb/catalog/catalog.h"
#include "minidb/storage/table_storage.h"

#include <filesystem>
#include <memory>

namespace minidb {

class RealStorageAdapter final : public ITableStorage {
public:
    [[nodiscard]] static Result<std::unique_ptr<RealStorageAdapter>> open(
        const std::filesystem::path& database_directory);

    ~RealStorageAdapter() override;
    RealStorageAdapter(const RealStorageAdapter&) = delete;
    RealStorageAdapter& operator=(const RealStorageAdapter&) = delete;

    [[nodiscard]] Result<TableId> create_table(
        std::string_view name,
        std::span<const ColumnDefinition> columns) override;
    [[nodiscard]] Result<TableSchema> get_schema(
        TableId table_id) const override;
    [[nodiscard]] Result<RID> insert_row(
        TableId table_id, const Row& row) override;
    [[nodiscard]] Result<std::unique_ptr<RowCursor>> scan_rows(
        TableId table_id) const override;
    [[nodiscard]] Result<std::optional<RowRecord>> get_row_by_rid(
        TableId table_id, RID rid) const override;

    [[nodiscard]] Status create_index(
        TableId table_id, std::string_view name,
        std::span<const std::uint32_t> key_columns) override;
    [[nodiscard]] Result<bool> has_index(
        TableId table_id, std::string_view name) const override;
    [[nodiscard]] Result<std::vector<RID>> index_lookup(
        TableId table_id, std::string_view name,
        const Value& key) const override;

    [[nodiscard]] const Catalog& catalog() const noexcept { return *catalog_; }

private:
    explicit RealStorageAdapter(std::unique_ptr<Catalog> catalog);

    [[nodiscard]] std::filesystem::path table_path(
        const TableMetadata& table) const;

    std::unique_ptr<Catalog> catalog_;
};

} // namespace minidb

#endif // MINIDB_STORAGE_REAL_STORAGE_ADAPTER_H
