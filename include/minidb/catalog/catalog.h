#ifndef MINIDB_CATALOG_CATALOG_H
#define MINIDB_CATALOG_CATALOG_H

#include "minidb/catalog/metadata.h"
#include "minidb/common/result.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace minidb {

// 持久化系统目录：维护表、字段、存储文件和索引定义。
// Catalog 只管理元数据，不负责读写表记录；物理记录访问由 StorageAdapter 完成。
class Catalog {
public:
    // 打开数据库目录；目录不存在时创建，已有目录则从 catalog.db 恢复元数据。
    [[nodiscard]] static Result<std::unique_ptr<Catalog>> open(
        const std::filesystem::path& database_directory);

    ~Catalog();
    Catalog(const Catalog&) = delete;
    Catalog& operator=(const Catalog&) = delete;

    // 返回下一候选 ID；真正推进计数器发生在 register_* 成功后。
    [[nodiscard]] TableId next_table_id() const noexcept;
    [[nodiscard]] IndexId next_index_id() const noexcept;

    [[nodiscard]] Status register_table(TableMetadata metadata);
    [[nodiscard]] Result<TableMetadata> find_table(TableId id) const;
    [[nodiscard]] Result<TableMetadata> find_table(
        std::string_view name) const;
    [[nodiscard]] std::vector<TableMetadata> list_tables() const;

    // 为真实物理索引预留。仅登记元数据并不会让索引对优化器可用；
    // 只有状态为 Available 且存储后端确认可用时才允许选择 IndexScan。
    [[nodiscard]] Status register_index(IndexMetadata metadata);
    [[nodiscard]] Result<IndexMetadata> find_index(
        TableId table_id, std::string_view name) const;
    [[nodiscard]] std::vector<IndexMetadata> list_indexes(
        TableId table_id) const;
    [[nodiscard]] bool has_available_index(
        TableId table_id, std::string_view name) const;

    [[nodiscard]] const std::filesystem::path& database_directory() const;

private:
    struct Impl;

    Catalog(std::filesystem::path database_directory,
            std::unique_ptr<Impl> impl);

    [[nodiscard]] Status load();

    std::filesystem::path database_directory_;
    std::unique_ptr<Impl> impl_;
};

} // namespace minidb

#endif // MINIDB_CATALOG_CATALOG_H
