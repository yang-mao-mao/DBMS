#ifndef MINIDB_CATALOG_METADATA_H
#define MINIDB_CATALOG_METADATA_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace minidb {

using TableId = std::uint64_t;
using IndexId = std::uint64_t;

enum class DataType : std::uint8_t {
    Int64 = 1,
    Varchar = 2,
};

struct ColumnDefinition {
    std::string name;
    DataType type{DataType::Int64};
    std::size_t max_length{0};
    bool nullable{false};

    bool operator==(const ColumnDefinition&) const = default;
};

struct TableSchema {
    std::string name;
    std::vector<ColumnDefinition> columns;

    bool operator==(const TableSchema&) const = default;
};

struct TableMetadata {
    TableId id{0};
    TableSchema schema;
    std::filesystem::path storage_file;

    bool operator==(const TableMetadata&) const = default;
};

enum class IndexState : std::uint8_t {
    Building = 1,
    Available = 2,
    Failed = 3,
};

struct IndexMetadata {
    IndexId id{0};
    TableId table_id{0};
    std::string name;
    std::vector<std::uint32_t> key_columns;
    std::filesystem::path storage_file;
    IndexState state{IndexState::Building};

    bool operator==(const IndexMetadata&) const = default;
};

} // namespace minidb

#endif // MINIDB_CATALOG_METADATA_H
