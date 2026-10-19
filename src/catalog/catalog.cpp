#include "minidb/catalog/catalog.h"

#include "DiskManager.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <ranges>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace minidb {
namespace {

constexpr std::uint32_t CATALOG_MAGIC = 0x31544143u; // "CAT1"
constexpr std::uint16_t CATALOG_VERSION = 1;
constexpr std::uint8_t TABLE_RECORD = 1;
constexpr std::uint8_t INDEX_RECORD = 2;
constexpr std::size_t MAX_PAGE_RECORD_SIZE =
    PAGE_SIZE - sizeof(PageHeader) - sizeof(Slot);

template <typename UInt>
void append_integer(std::vector<std::byte>& output, UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        output.push_back(static_cast<std::byte>(value & 0xFFu));
        value >>= 8u;
    }
}

template <typename UInt>
bool read_integer(std::span<const std::byte> input,
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

bool append_string(std::vector<std::byte>& output, std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    append_integer(output, static_cast<std::uint32_t>(value.size()));
    for (const unsigned char byte : value) {
        output.push_back(static_cast<std::byte>(byte));
    }
    return true;
}

bool read_string(std::span<const std::byte> input,
                 std::size_t& offset, std::string& value) {
    std::uint32_t length = 0;
    if (!read_integer(input, offset, length)
        || offset > input.size() || input.size() - offset < length) {
        return false;
    }
    const auto* begin = reinterpret_cast<const char*>(input.data() + offset);
    value.assign(begin, length);
    offset += length;
    return true;
}

Status validate_schema(const TableSchema& schema) {
    if (schema.name.empty()) {
        return Status::invalid_argument("table name cannot be empty");
    }
    if (schema.columns.empty()) {
        return Status::invalid_argument("table must contain at least one column");
    }
    if (schema.columns.size() > std::numeric_limits<std::uint16_t>::max()) {
        return Status::invalid_argument("table has too many columns");
    }

    std::unordered_set<std::string> names;
    for (const auto& column : schema.columns) {
        if (column.name.empty()) {
            return Status::invalid_argument("column name cannot be empty");
        }
        if (!names.insert(column.name).second) {
            return Status::invalid_argument(
                "duplicate column name: " + column.name);
        }
        switch (column.type) {
            case DataType::Int64:
                if (column.max_length != 0) {
                    return Status::invalid_argument(
                        "INT column max_length must be zero");
                }
                break;
            case DataType::Varchar:
                if (column.max_length == 0
                    || column.max_length
                           > std::numeric_limits<std::uint32_t>::max()) {
                    return Status::invalid_argument(
                        "VARCHAR max_length must be between 1 and UINT32_MAX");
                }
                break;
            default:
                return Status::invalid_argument("unsupported column type");
        }
    }
    return Status::ok_status();
}

bool is_safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) return false;
    for (const auto& component : path) {
        if (component == "." || component == "..") return false;
    }
    return true;
}

Result<std::vector<std::byte>> encode_table(const TableMetadata& table) {
    std::vector<std::byte> output;
    append_integer(output, CATALOG_MAGIC);
    append_integer(output, CATALOG_VERSION);
    append_integer(output, TABLE_RECORD);
    append_integer(output, static_cast<std::uint8_t>(0));
    append_integer(output, table.id);
    const std::string file = table.storage_file.generic_string();
    if (!append_string(output, table.schema.name)
        || !append_string(output, file)
        || table.schema.columns.size()
               > std::numeric_limits<std::uint32_t>::max()) {
        return Status::invalid_argument("catalog table metadata is too large");
    }
    append_integer(
        output, static_cast<std::uint32_t>(table.schema.columns.size()));
    for (const auto& column : table.schema.columns) {
        if (!append_string(output, column.name)
            || column.max_length
                   > std::numeric_limits<std::uint32_t>::max()) {
            return Status::invalid_argument("catalog column metadata is too large");
        }
        append_integer(output, static_cast<std::uint8_t>(column.type));
        append_integer(output, static_cast<std::uint8_t>(column.nullable ? 1 : 0));
        append_integer(output, static_cast<std::uint16_t>(0));
        append_integer(output, static_cast<std::uint32_t>(column.max_length));
    }
    return output;
}

Result<std::vector<std::byte>> encode_index(const IndexMetadata& index) {
    std::vector<std::byte> output;
    append_integer(output, CATALOG_MAGIC);
    append_integer(output, CATALOG_VERSION);
    append_integer(output, INDEX_RECORD);
    append_integer(output, static_cast<std::uint8_t>(0));
    append_integer(output, index.id);
    append_integer(output, index.table_id);
    const std::string file = index.storage_file.generic_string();
    if (!append_string(output, index.name)
        || !append_string(output, file)
        || index.key_columns.size()
               > std::numeric_limits<std::uint32_t>::max()) {
        return Status::invalid_argument("catalog index metadata is too large");
    }
    append_integer(output, static_cast<std::uint8_t>(index.state));
    append_integer(
        output, static_cast<std::uint32_t>(index.key_columns.size()));
    for (const std::uint32_t column : index.key_columns) {
        append_integer(output, column);
    }
    return output;
}

using CatalogRecord = std::variant<TableMetadata, IndexMetadata>;

Result<CatalogRecord> decode_record(std::span<const std::byte> bytes) {
    std::size_t offset = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint8_t kind = 0;
    std::uint8_t reserved = 0;
    if (!read_integer(bytes, offset, magic)
        || !read_integer(bytes, offset, version)
        || !read_integer(bytes, offset, kind)
        || !read_integer(bytes, offset, reserved)) {
        return Status::corruption("catalog record header is truncated");
    }
    if (magic != CATALOG_MAGIC || version != CATALOG_VERSION || reserved != 0) {
        return Status::corruption("catalog record header is invalid");
    }

    if (kind == TABLE_RECORD) {
        TableMetadata table;
        std::string file;
        std::uint32_t column_count = 0;
        if (!read_integer(bytes, offset, table.id)
            || !read_string(bytes, offset, table.schema.name)
            || !read_string(bytes, offset, file)
            || !read_integer(bytes, offset, column_count)) {
            return Status::corruption("catalog table record is truncated");
        }
        if (column_count > bytes.size()) {
            return Status::corruption("catalog column count is invalid");
        }
        table.storage_file = std::filesystem::path(file);
        table.schema.columns.reserve(column_count);
        for (std::uint32_t i = 0; i < column_count; ++i) {
            ColumnDefinition column;
            std::uint8_t type = 0;
            std::uint8_t nullable = 0;
            std::uint16_t column_reserved = 0;
            std::uint32_t max_length = 0;
            if (!read_string(bytes, offset, column.name)
                || !read_integer(bytes, offset, type)
                || !read_integer(bytes, offset, nullable)
                || !read_integer(bytes, offset, column_reserved)
                || !read_integer(bytes, offset, max_length)) {
                return Status::corruption("catalog column record is truncated");
            }
            if ((type != static_cast<std::uint8_t>(DataType::Int64)
                 && type != static_cast<std::uint8_t>(DataType::Varchar))
                || nullable > 1 || column_reserved != 0) {
                return Status::corruption("catalog column record is invalid");
            }
            column.type = static_cast<DataType>(type);
            column.nullable = nullable != 0;
            column.max_length = max_length;
            table.schema.columns.push_back(std::move(column));
        }
        if (offset != bytes.size()) {
            return Status::corruption("catalog table record has trailing bytes");
        }
        const Status schema_status = validate_schema(table.schema);
        if (!schema_status.ok() || table.id == 0
            || table.id == std::numeric_limits<TableId>::max()
            || !is_safe_relative_path(table.storage_file)) {
            return Status::corruption("catalog table metadata is invalid");
        }
        return CatalogRecord{std::move(table)};
    }

    if (kind == INDEX_RECORD) {
        IndexMetadata index;
        std::string file;
        std::uint8_t state = 0;
        std::uint32_t key_count = 0;
        if (!read_integer(bytes, offset, index.id)
            || !read_integer(bytes, offset, index.table_id)
            || !read_string(bytes, offset, index.name)
            || !read_string(bytes, offset, file)
            || !read_integer(bytes, offset, state)
            || !read_integer(bytes, offset, key_count)) {
            return Status::corruption("catalog index record is truncated");
        }
        if (key_count > (bytes.size() - offset) / sizeof(std::uint32_t)) {
            return Status::corruption("catalog index key count is invalid");
        }
        index.storage_file = std::filesystem::path(file);
        if (state < static_cast<std::uint8_t>(IndexState::Building)
            || state > static_cast<std::uint8_t>(IndexState::Failed)) {
            return Status::corruption("catalog index state is invalid");
        }
        index.state = static_cast<IndexState>(state);
        index.key_columns.reserve(key_count);
        for (std::uint32_t i = 0; i < key_count; ++i) {
            std::uint32_t column = 0;
            if (!read_integer(bytes, offset, column)) {
                return Status::corruption("catalog index key list is truncated");
            }
            index.key_columns.push_back(column);
        }
        if (offset != bytes.size() || index.id == 0 || index.table_id == 0
            || index.id == std::numeric_limits<IndexId>::max()
            || index.name.empty() || index.key_columns.empty()
            || !is_safe_relative_path(index.storage_file)) {
            return Status::corruption("catalog index metadata is invalid");
        }
        return CatalogRecord{std::move(index)};
    }

    return Status::corruption("unknown catalog record type");
}

Status append_record(DiskManager& manager, std::span<const std::byte> bytes) {
    if (bytes.empty() || bytes.size() > MAX_PAGE_RECORD_SIZE
        || bytes.size() > std::numeric_limits<std::uint16_t>::max()) {
        return Status::record_too_large(
            "catalog record does not fit in one storage page");
    }

    for (std::uint32_t page_id = 1; page_id < manager.page_count(); ++page_id) {
        Page page{};
        if (!manager.read_page(page_id, page)) {
            return Status::io_error("failed to read a catalog page");
        }
        if (page.header.page_type != PageType::DATA) continue;
        const int slot = page.insert_record(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::uint16_t>(bytes.size()));
        if (slot < 0) continue;
        if (!manager.write_page(page_id, page)) {
            return Status::io_error("failed to write a catalog page");
        }
        return Status::ok_status();
    }

    const std::uint32_t page_id = manager.allocate_page(PageType::DATA);
    if (page_id == INVALID_PAGE) {
        return Status::no_space("failed to allocate a catalog page");
    }
    Page page{};
    if (!manager.read_page(page_id, page)) {
        return Status::io_error("failed to read a new catalog page");
    }
    const int slot = page.insert_record(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::uint16_t>(bytes.size()));
    if (slot < 0 || !manager.write_page(page_id, page)) {
        return Status::io_error("failed to initialize a catalog page");
    }
    return Status::ok_status();
}

} // namespace

struct Catalog::Impl {
    std::unique_ptr<DiskManager> manager;
    std::unordered_map<TableId, TableMetadata> tables;
    std::unordered_map<std::string, TableId> table_names;
    std::unordered_map<IndexId, IndexMetadata> indexes;
    TableId next_table_id{1};
    IndexId next_index_id{1};
};

Catalog::Catalog(std::filesystem::path database_directory,
                 std::unique_ptr<Impl> impl)
    : database_directory_(std::move(database_directory)),
      impl_(std::move(impl)) {}

Catalog::~Catalog() = default;

Result<std::unique_ptr<Catalog>> Catalog::open(
    const std::filesystem::path& database_directory) {
    std::error_code error;
    std::filesystem::create_directories(database_directory, error);
    if (error) {
        return Status::io_error(
            "failed to create database directory: " + error.message());
    }

    auto manager = DiskManager::open(
        (database_directory / "catalog.mdb").string());
    if (!manager) {
        return Status::io_error("failed to open catalog.mdb");
    }
    auto impl = std::make_unique<Impl>();
    impl->manager = std::move(manager);
    auto catalog = std::unique_ptr<Catalog>(
        new Catalog(database_directory, std::move(impl)));
    const Status status = catalog->load();
    if (!status.ok()) return status;
    return catalog;
}

Status Catalog::load() {
    for (std::uint32_t page_id = 1;
         page_id < impl_->manager->page_count(); ++page_id) {
        Page page{};
        if (!impl_->manager->read_page(page_id, page)) {
            return Status::corruption("failed to read a catalog page");
        }
        if (page.header.page_type == PageType::FREE) continue;
        if (page.header.page_type != PageType::DATA) {
            return Status::corruption("catalog contains a non-DATA page");
        }
        for (std::uint16_t slot = 0; slot < page.slot_count(); ++slot) {
            std::uint16_t length = 0;
            const char* data = page.get_record(slot, length);
            if (data == nullptr) {
                if (page.is_deleted(slot)) continue;
                return Status::corruption("catalog contains an invalid slot");
            }
            const auto bytes = std::span(
                reinterpret_cast<const std::byte*>(data), length);
            auto decoded = decode_record(bytes);
            if (!decoded.ok()) return decoded.status();

            if (auto* table = std::get_if<TableMetadata>(&decoded.value())) {
                if (impl_->tables.contains(table->id)
                    || impl_->table_names.contains(table->schema.name)) {
                    return Status::corruption(
                        "catalog contains duplicate table metadata");
                }
                impl_->next_table_id = std::max(
                    impl_->next_table_id, table->id + 1);
                impl_->table_names.emplace(table->schema.name, table->id);
                impl_->tables.emplace(table->id, std::move(*table));
            } else {
                auto index = std::get<IndexMetadata>(std::move(decoded.value()));
                if (impl_->indexes.contains(index.id)) {
                    return Status::corruption(
                        "catalog contains duplicate index metadata");
                }
                impl_->next_index_id = std::max(
                    impl_->next_index_id, index.id + 1);
                impl_->indexes.emplace(index.id, std::move(index));
            }
        }
    }

    for (const auto& [id, index] : impl_->indexes) {
        (void)id;
        const auto table = impl_->tables.find(index.table_id);
        if (table == impl_->tables.end()) {
            return Status::corruption("index refers to an unknown table");
        }
        if (std::ranges::any_of(index.key_columns, [&](std::uint32_t column) {
                return column >= table->second.schema.columns.size();
            })) {
            return Status::corruption("index refers to an unknown column");
        }
    }
    return Status::ok_status();
}

TableId Catalog::next_table_id() const noexcept {
    return impl_->next_table_id;
}

IndexId Catalog::next_index_id() const noexcept {
    return impl_->next_index_id;
}

Status Catalog::register_table(TableMetadata metadata) {
    const Status schema_status = validate_schema(metadata.schema);
    if (!schema_status.ok()) return schema_status;
    if (metadata.id == 0 || metadata.id != impl_->next_table_id) {
        return Status::invalid_argument("table id is not the next catalog id");
    }
    if (!is_safe_relative_path(metadata.storage_file)) {
        return Status::invalid_argument(
            "table storage file must be a non-empty relative path");
    }
    if (impl_->table_names.contains(metadata.schema.name)) {
        return Status::already_exists(
            "table already exists: " + metadata.schema.name);
    }
    auto encoded = encode_table(metadata);
    if (!encoded.ok()) return encoded.status();
    const Status persisted = append_record(*impl_->manager, encoded.value());
    if (!persisted.ok()) return persisted;

    impl_->table_names.emplace(metadata.schema.name, metadata.id);
    impl_->tables.emplace(metadata.id, std::move(metadata));
    ++impl_->next_table_id;
    return Status::ok_status();
}

Result<TableMetadata> Catalog::find_table(TableId id) const {
    const auto it = impl_->tables.find(id);
    if (it == impl_->tables.end()) {
        return Status::not_found("table id does not exist");
    }
    return it->second;
}

Result<TableMetadata> Catalog::find_table(std::string_view name) const {
    const auto it = impl_->table_names.find(std::string(name));
    if (it == impl_->table_names.end()) {
        return Status::not_found("table does not exist: " + std::string(name));
    }
    return impl_->tables.at(it->second);
}

std::vector<TableMetadata> Catalog::list_tables() const {
    std::vector<TableMetadata> tables;
    tables.reserve(impl_->tables.size());
    for (const auto& [id, table] : impl_->tables) {
        (void)id;
        tables.push_back(table);
    }
    std::ranges::sort(tables, {}, &TableMetadata::id);
    return tables;
}

Status Catalog::register_index(IndexMetadata metadata) {
    if (metadata.id == 0 || metadata.id != impl_->next_index_id
        || metadata.name.empty() || metadata.key_columns.empty()) {
        return Status::invalid_argument("index metadata is incomplete");
    }
    if (!is_safe_relative_path(metadata.storage_file)) {
        return Status::invalid_argument(
            "index storage file must be a safe relative path");
    }
    const auto table = impl_->tables.find(metadata.table_id);
    if (table == impl_->tables.end()) {
        return Status::not_found("index table does not exist");
    }
    if (std::ranges::any_of(metadata.key_columns, [&](std::uint32_t column) {
            return column >= table->second.schema.columns.size();
        })) {
        return Status::invalid_argument("index contains an invalid key column");
    }
    for (const auto& [id, existing] : impl_->indexes) {
        (void)id;
        if (existing.table_id == metadata.table_id
            && existing.name == metadata.name) {
            return Status::already_exists("index already exists");
        }
    }
    auto encoded = encode_index(metadata);
    if (!encoded.ok()) return encoded.status();
    const Status persisted = append_record(*impl_->manager, encoded.value());
    if (!persisted.ok()) return persisted;
    impl_->indexes.emplace(metadata.id, std::move(metadata));
    ++impl_->next_index_id;
    return Status::ok_status();
}

Result<IndexMetadata> Catalog::find_index(
    TableId table_id, std::string_view name) const {
    for (const auto& [id, index] : impl_->indexes) {
        (void)id;
        if (index.table_id == table_id && index.name == name) return index;
    }
    return Status::not_found("index does not exist: " + std::string(name));
}

std::vector<IndexMetadata> Catalog::list_indexes(TableId table_id) const {
    std::vector<IndexMetadata> indexes;
    for (const auto& [id, index] : impl_->indexes) {
        (void)id;
        if (index.table_id == table_id) indexes.push_back(index);
    }
    std::ranges::sort(indexes, {}, &IndexMetadata::id);
    return indexes;
}

bool Catalog::has_available_index(
    TableId table_id, std::string_view name) const {
    const auto index = find_index(table_id, name);
    return index.ok() && index.value().state == IndexState::Available;
}

const std::filesystem::path& Catalog::database_directory() const {
    return database_directory_;
}

} // namespace minidb
