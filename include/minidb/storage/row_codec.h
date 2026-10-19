#ifndef MINIDB_STORAGE_ROW_CODEC_H
#define MINIDB_STORAGE_ROW_CODEC_H

#include "minidb/catalog/metadata.h"
#include "minidb/common/result.h"
#include "minidb/storage/row.h"

#include <cstddef>
#include <span>
#include <vector>

namespace minidb {

class RowCodec {
public:
    static constexpr std::uint16_t FORMAT_VERSION = 1;

    [[nodiscard]] static Result<std::vector<std::byte>> encode(
        const TableSchema& schema, std::span<const Value> values);

    [[nodiscard]] static Result<Row> decode(
        const TableSchema& schema, std::span<const std::byte> bytes);
};

} // namespace minidb

#endif // MINIDB_STORAGE_ROW_CODEC_H
