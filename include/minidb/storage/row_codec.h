#ifndef MINIDB_STORAGE_ROW_CODEC_H
#define MINIDB_STORAGE_ROW_CODEC_H

#include "minidb/catalog/metadata.h"
#include "minidb/common/result.h"
#include "minidb/storage/row.h"

#include <cstddef>
#include <span>
#include <vector>

namespace minidb {

// 关系行与页内二进制记录之间的唯一编解码入口。
// 编码中带有格式版本，保证未来升级时能够拒绝不兼容的数据。
class RowCodec {
public:
    static constexpr std::uint16_t FORMAT_VERSION = 1;

    // 编码前校验字段数量、类型、NULL 约束及 VARCHAR 长度。
    [[nodiscard]] static Result<std::vector<std::byte>> encode(
        const TableSchema& schema, std::span<const Value> values);

    // 解码时同时依据 Schema 校验记录内容，损坏数据返回 Corruption。
    [[nodiscard]] static Result<Row> decode(
        const TableSchema& schema, std::span<const std::byte> bytes);
};

} // namespace minidb

#endif // MINIDB_STORAGE_ROW_CODEC_H
