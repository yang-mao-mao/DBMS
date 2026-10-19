#ifndef MINIDB_STORAGE_ROW_H
#define MINIDB_STORAGE_ROW_H

#include "minidb/storage/rid.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace minidb {

using Value = std::variant<std::monostate, std::int64_t, std::string>;
using Row = std::vector<Value>;

struct RowRecord {
    RID rid;
    Row row;
};

} // namespace minidb

#endif // MINIDB_STORAGE_ROW_H
