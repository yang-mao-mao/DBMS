#ifndef MINIDB_STORAGE_ROW_H
#define MINIDB_STORAGE_ROW_H

#include "minidb/storage/rid.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace minidb {

// monostate 表示 NULL；其余分支分别对应 INT64 和 VARCHAR。
using Value = std::variant<std::monostate, std::int64_t, std::string>;
// Row 中 Value 的顺序必须与 TableSchema::columns 完全一致。
using Row = std::vector<Value>;

// 扫描或按 RID 查询时同时返回物理位置和解码后的逻辑行。
struct RowRecord {
    RID rid;
    Row row;
};

} // namespace minidb

#endif // MINIDB_STORAGE_ROW_H
