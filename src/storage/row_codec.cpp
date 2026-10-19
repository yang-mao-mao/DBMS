#include "minidb/storage/row_codec.h"

#include <bit>
#include <limits>
#include <string>
#include <type_traits>

namespace minidb {
namespace {

constexpr std::uint32_t ROW_MAGIC = 0x31574F52u; // "ROW1" in little endian.

template <typename UInt>
void append_little_endian(std::vector<std::byte>& output, UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        output.push_back(static_cast<std::byte>(value & 0xFFu));
        value >>= 8u;
    }
}

template <typename UInt>
bool read_little_endian(std::span<const std::byte> input,
                        std::size_t& offset, UInt& value) {
    static_assert(std::is_unsigned_v<UInt>);
    if (offset > input.size() || input.size() - offset < sizeof(UInt)) {
        return false;
    }
    value = 0;
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        value |= static_cast<UInt>(std::to_integer<unsigned int>(input[offset + i]))
                 << (8u * i);
    }
    offset += sizeof(UInt);
    return true;
}

Status invalid_column(std::size_t column, std::string message) {
    return Status::invalid_argument(
        "column " + std::to_string(column) + ": " + std::move(message));
}

Status corrupt_column(std::size_t column, std::string message) {
    return Status::corruption(
        "column " + std::to_string(column) + ": " + std::move(message));
}

} // namespace

Result<std::vector<std::byte>> RowCodec::encode(
    const TableSchema& schema, std::span<const Value> values) {
    if (values.size() != schema.columns.size()) {
        return Status::invalid_argument(
            "row value count does not match the table schema");
    }
    if (schema.columns.size() > std::numeric_limits<std::uint16_t>::max()) {
        return Status::invalid_argument("schema has too many columns");
    }

    const std::size_t null_bytes = (schema.columns.size() + 7u) / 8u;
    std::vector<std::byte> output;
    output.reserve(8u + null_bytes + values.size() * 8u);
    append_little_endian(output, ROW_MAGIC);
    append_little_endian(output, FORMAT_VERSION);
    append_little_endian(
        output, static_cast<std::uint16_t>(schema.columns.size()));

    const std::size_t null_offset = output.size();
    output.resize(output.size() + null_bytes, std::byte{0});

    for (std::size_t i = 0; i < values.size(); ++i) {
        const ColumnDefinition& column = schema.columns[i];
        const Value& value = values[i];
        if (std::holds_alternative<std::monostate>(value)) {
            if (!column.nullable) {
                return invalid_column(i, "NULL is not allowed");
            }
            output[null_offset + i / 8u] |=
                static_cast<std::byte>(1u << (i % 8u));
            continue;
        }

        switch (column.type) {
            case DataType::Int64: {
                const auto* integer = std::get_if<std::int64_t>(&value);
                if (integer == nullptr) {
                    return invalid_column(i, "expected INT");
                }
                append_little_endian(
                    output, std::bit_cast<std::uint64_t>(*integer));
                break;
            }
            case DataType::Varchar: {
                const auto* string = std::get_if<std::string>(&value);
                if (string == nullptr) {
                    return invalid_column(i, "expected VARCHAR");
                }
                if (string->size() > column.max_length) {
                    return invalid_column(i, "VARCHAR length exceeds max_length");
                }
                if (string->size() > std::numeric_limits<std::uint32_t>::max()) {
                    return invalid_column(i, "VARCHAR value is too large");
                }
                append_little_endian(
                    output, static_cast<std::uint32_t>(string->size()));
                for (const unsigned char byte : *string) {
                    output.push_back(static_cast<std::byte>(byte));
                }
                break;
            }
            default:
                return invalid_column(i, "unsupported data type");
        }
    }
    return output;
}

Result<Row> RowCodec::decode(const TableSchema& schema,
                             std::span<const std::byte> bytes) {
    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t column_count = 0;
    if (!read_little_endian(bytes, offset, magic)
        || !read_little_endian(bytes, offset, version)
        || !read_little_endian(bytes, offset, column_count)) {
        return Status::corruption("row header is truncated");
    }
    if (magic != ROW_MAGIC) {
        return Status::corruption("row magic does not match");
    }
    if (version != FORMAT_VERSION) {
        return Status::corruption("unsupported row format version");
    }
    if (column_count != schema.columns.size()) {
        return Status::corruption("row column count does not match schema");
    }

    const std::size_t null_bytes = (schema.columns.size() + 7u) / 8u;
    if (offset > bytes.size() || bytes.size() - offset < null_bytes) {
        return Status::corruption("row null bitmap is truncated");
    }
    const auto null_bitmap = bytes.subspan(offset, null_bytes);
    offset += null_bytes;

    Row row;
    row.reserve(schema.columns.size());
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        const ColumnDefinition& column = schema.columns[i];
        const bool is_null =
            (std::to_integer<unsigned int>(null_bitmap[i / 8u])
             & (1u << (i % 8u))) != 0;
        if (is_null) {
            if (!column.nullable) {
                return corrupt_column(i, "NULL stored in a NOT NULL column");
            }
            row.emplace_back(std::monostate{});
            continue;
        }

        switch (column.type) {
            case DataType::Int64: {
                std::uint64_t bits = 0;
                if (!read_little_endian(bytes, offset, bits)) {
                    return corrupt_column(i, "INT payload is truncated");
                }
                row.emplace_back(std::bit_cast<std::int64_t>(bits));
                break;
            }
            case DataType::Varchar: {
                std::uint32_t length = 0;
                if (!read_little_endian(bytes, offset, length)) {
                    return corrupt_column(i, "VARCHAR length is truncated");
                }
                if (length > column.max_length) {
                    return corrupt_column(i, "VARCHAR exceeds schema max_length");
                }
                if (offset > bytes.size() || bytes.size() - offset < length) {
                    return corrupt_column(i, "VARCHAR payload is truncated");
                }
                const auto* begin = reinterpret_cast<const char*>(
                    bytes.data() + offset);
                row.emplace_back(std::string(begin, length));
                offset += length;
                break;
            }
            default:
                return corrupt_column(i, "unsupported data type");
        }
    }

    if (offset != bytes.size()) {
        return Status::corruption("row contains trailing bytes");
    }
    return row;
}

} // namespace minidb
