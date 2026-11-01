#ifndef MINIDB_STORAGE_REAL_STORAGE_ADAPTER_H
#define MINIDB_STORAGE_REAL_STORAGE_ADAPTER_H

#include "minidb/catalog/catalog.h"
#include "minidb/storage/table_storage.h"

#include <filesystem>
#include <memory>

namespace minidb {

// 将“一表一文件”的物理存储接口适配成 ITableStorage。
// 上层只处理 TableId、RID 和 Row，不直接接触 DiskManager 或页布局。
class RealStorageAdapter final : public ITableStorage {
public:
    // 打开 Catalog；具体表文件按需打开，避免长期持有底层文件句柄。
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
    [[nodiscard]] Status delete_row(TableId table_id, RID rid) override;

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
    [[nodiscard]] std::filesystem::path index_path(
        const IndexMetadata& index) const;

    std::unique_ptr<Catalog> catalog_;
};

} // namespace minidb

#endif // MINIDB_STORAGE_REAL_STORAGE_ADAPTER_H
