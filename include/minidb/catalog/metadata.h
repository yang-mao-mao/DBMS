#ifndef MINIDB_CATALOG_METADATA_H
#define MINIDB_CATALOG_METADATA_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace minidb {

// TableId/IndexId 是 Catalog 持久化的稳定逻辑标识，不等同于页号。
using TableId = std::uint64_t;
using IndexId = std::uint64_t;

// 当前关系模型支持的最小字段类型集合。
enum class DataType : std::uint8_t {
    Int64 = 1,
    Varchar = 2,
};

struct ColumnDefinition {
    std::string name;
    DataType type{DataType::Int64};
    std::size_t max_length{0}; // 仅 VARCHAR 使用；INT64 应为 0。
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
    std::filesystem::path storage_file; // 相对数据库目录的一表一文件路径。

    bool operator==(const TableMetadata&) const = default;
};

// 索引状态用于防止“只有元数据、没有物理索引”被优化器误用。
enum class IndexState : std::uint8_t {
    Building = 1,
    Available = 2,
    Failed = 3,
};

struct IndexMetadata {
    IndexId id{0};
    TableId table_id{0};
    std::string name;
    std::vector<std::uint32_t> key_columns; // 字段在表 Schema 中的下标。
    std::filesystem::path storage_file;
    IndexState state{IndexState::Building};
    // BPlusTree::create() 返回的稳定入口页。该页号由物理索引分配，
    // 不能由上层根据文件布局猜测。
    std::uint32_t header_page_id{0};

    bool operator==(const IndexMetadata&) const = default;
};

} // namespace minidb

#endif // MINIDB_CATALOG_METADATA_H
