#ifndef MINIDB_CATALOG_CATALOG_H
#define MINIDB_CATALOG_CATALOG_H

#include "minidb/catalog/metadata.h"
#include "minidb/common/result.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace minidb {

class Catalog {
public:
    [[nodiscard]] static Result<std::unique_ptr<Catalog>> open(
        const std::filesystem::path& database_directory);

    ~Catalog();
    Catalog(const Catalog&) = delete;
    Catalog& operator=(const Catalog&) = delete;

    [[nodiscard]] TableId next_table_id() const noexcept;
    [[nodiscard]] IndexId next_index_id() const noexcept;

    [[nodiscard]] Status register_table(TableMetadata metadata);
    [[nodiscard]] Result<TableMetadata> find_table(TableId id) const;
    [[nodiscard]] Result<TableMetadata> find_table(
        std::string_view name) const;
    [[nodiscard]] std::vector<TableMetadata> list_tables() const;

    // Reserved for a real physical index backend. Registering metadata alone
    // does not make an index available to the query planner.
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
