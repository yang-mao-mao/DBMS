#include "minidb/storage/real_storage_adapter.h"

#include "DiskManager.h"
#include "minidb/storage/row_codec.h"

#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace minidb {
namespace {

constexpr std::size_t MAX_PAGE_RECORD_SIZE =
    PAGE_SIZE - sizeof(PageHeader) - sizeof(Slot);

class DiskRowCursor final : public RowCursor {
public:
    DiskRowCursor(std::unique_ptr<DiskManager> manager, TableSchema schema)
        : manager_(std::move(manager)), schema_(std::move(schema)) {}

    Result<std::optional<RowRecord>> next() override {
        while (true) {
            if (page_loaded_) {
                while (next_slot_ < page_.slot_count()) {
                    const std::uint16_t slot = next_slot_++;
                    std::uint16_t length = 0;
                    const char* data = page_.get_record(slot, length);
                    if (data == nullptr) {
                        if (page_.is_deleted(slot)) continue;
                        return Status::corruption(
                            "table page contains an invalid record slot");
                    }
                    const auto bytes = std::span(
                        reinterpret_cast<const std::byte*>(data), length);
                    auto decoded = RowCodec::decode(schema_, bytes);
                    if (!decoded.ok()) return decoded.status();
                    return std::optional<RowRecord>{RowRecord{
                        RID{current_page_id_, slot},
                        std::move(decoded).value()}};
                }
                page_loaded_ = false;
            }

            if (next_page_id_ >= manager_->page_count()) {
                return std::optional<RowRecord>{};
            }
            current_page_id_ = next_page_id_++;
            if (!manager_->read_page(current_page_id_, page_)) {
                return Status::io_error("failed to read a table page during scan");
            }
            if (page_.header.page_type != PageType::DATA) continue;
            next_slot_ = 0;
            page_loaded_ = true;
        }
    }

private:
    std::unique_ptr<DiskManager> manager_;
    TableSchema schema_;
    std::uint32_t next_page_id_{1};
    std::uint32_t current_page_id_{0};
    std::uint16_t next_slot_{0};
    Page page_{};
    bool page_loaded_{false};
};

} // namespace

RealStorageAdapter::RealStorageAdapter(std::unique_ptr<Catalog> catalog)
    : catalog_(std::move(catalog)) {}

RealStorageAdapter::~RealStorageAdapter() = default;

Result<std::unique_ptr<RealStorageAdapter>> RealStorageAdapter::open(
    const std::filesystem::path& database_directory) {
    auto catalog = Catalog::open(database_directory);
    if (!catalog.ok()) return catalog.status();

    std::error_code error;
    std::filesystem::create_directories(
        database_directory / "tables", error);
    if (error) {
        return Status::io_error(
            "failed to create table directory: " + error.message());
    }
    std::filesystem::create_directories(
        database_directory / "indexes", error);
    if (error) {
        return Status::io_error(
            "failed to create index directory: " + error.message());
    }
    auto adapter = std::unique_ptr<RealStorageAdapter>(
        new RealStorageAdapter(std::move(catalog).value()));
    for (const auto& table : adapter->catalog_->list_tables()) {
        const auto path = adapter->table_path(table);
        const bool exists = std::filesystem::exists(path, error);
        if (error) {
            return Status::io_error(
                "failed to inspect a table file: " + error.message());
        }
        if (!exists) {
            return Status::corruption(
                "catalog refers to a missing table file: "
                + table.storage_file.generic_string());
        }
        auto manager = DiskManager::open(path.string());
        if (!manager) {
            return Status::corruption(
                "catalog refers to an invalid table file: "
                + table.storage_file.generic_string());
        }
    }
    return adapter;
}

std::filesystem::path RealStorageAdapter::table_path(
    const TableMetadata& table) const {
    return catalog_->database_directory() / table.storage_file;
}

Result<TableId> RealStorageAdapter::create_table(
    std::string_view name, std::span<const ColumnDefinition> columns) {
    const auto existing = catalog_->find_table(name);
    if (existing.ok()) {
        return Status::already_exists("table already exists: " + std::string(name));
    }
    if (existing.status().code() != ErrorCode::NotFound) {
        return existing.status();
    }

    const TableId id = catalog_->next_table_id();
    TableMetadata metadata;
    metadata.id = id;
    metadata.schema.name = std::string(name);
    metadata.schema.columns.assign(columns.begin(), columns.end());
    metadata.storage_file =
        std::filesystem::path("tables") /
        ("table_" + std::to_string(id) + ".mdb");

    const std::filesystem::path path = table_path(metadata);
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        if (error) {
            return Status::io_error(
                "failed to inspect table file: " + error.message());
        }
        return Status::already_exists(
            "table file already exists without matching catalog metadata");
    }

    auto manager = DiskManager::open(path.string());
    if (!manager) {
        return Status::io_error("failed to create table storage file");
    }
    manager.reset();

    const Status registered = catalog_->register_table(std::move(metadata));
    if (!registered.ok()) {
        std::filesystem::remove(path, error);
        return registered;
    }
    return id;
}

Result<TableSchema> RealStorageAdapter::get_schema(TableId table_id) const {
    auto table = catalog_->find_table(table_id);
    if (!table.ok()) return table.status();
    return table.value().schema;
}

Result<RID> RealStorageAdapter::insert_row(TableId table_id, const Row& row) {
    auto table_result = catalog_->find_table(table_id);
    if (!table_result.ok()) return table_result.status();
    const TableMetadata table = std::move(table_result).value();

    auto encoded = RowCodec::encode(table.schema, row);
    if (!encoded.ok()) return encoded.status();
    if (encoded.value().size() > MAX_PAGE_RECORD_SIZE
        || encoded.value().size() > std::numeric_limits<std::uint16_t>::max()) {
        return Status::record_too_large(
            "encoded row does not fit in one storage page");
    }

    auto manager = DiskManager::open(table_path(table).string());
    if (!manager) return Status::io_error("failed to open table storage file");

    const auto* data = reinterpret_cast<const char*>(encoded.value().data());
    const auto length = static_cast<std::uint16_t>(encoded.value().size());
    for (std::uint32_t page_id = 1; page_id < manager->page_count(); ++page_id) {
        Page page{};
        if (!manager->read_page(page_id, page)) {
            return Status::io_error("failed to read a table page");
        }
        if (page.header.page_type != PageType::DATA) continue;
        const int slot = page.insert_record(data, length);
        if (slot < 0) continue;
        if (!manager->write_page(page_id, page)) {
            return Status::io_error("failed to write a table page");
        }
        return RID{page_id, static_cast<std::uint16_t>(slot)};
    }

    const std::uint32_t page_id = manager->allocate_page(PageType::DATA);
    if (page_id == INVALID_PAGE) {
        return Status::no_space("failed to allocate a table page");
    }
    Page page{};
    if (!manager->read_page(page_id, page)) {
        return Status::io_error("failed to read a new table page");
    }
    const int slot = page.insert_record(data, length);
    if (slot < 0) {
        return Status::internal_error("new table page rejected a valid row");
    }
    if (!manager->write_page(page_id, page)) {
        return Status::io_error("failed to persist a new table page");
    }
    return RID{page_id, static_cast<std::uint16_t>(slot)};
}

Result<std::unique_ptr<RowCursor>> RealStorageAdapter::scan_rows(
    TableId table_id) const {
    auto table = catalog_->find_table(table_id);
    if (!table.ok()) return table.status();
    auto manager = DiskManager::open(table_path(table.value()).string());
    if (!manager) return Status::io_error("failed to open table storage file");
    return std::unique_ptr<RowCursor>(new DiskRowCursor(
        std::move(manager), table.value().schema));
}

Result<std::optional<RowRecord>> RealStorageAdapter::get_row_by_rid(
    TableId table_id, RID rid) const {
    auto table = catalog_->find_table(table_id);
    if (!table.ok()) return table.status();
    auto manager = DiskManager::open(table_path(table.value()).string());
    if (!manager) return Status::io_error("failed to open table storage file");
    if (rid.page_id == 0 || !manager->is_valid_page(rid.page_id)) {
        return std::optional<RowRecord>{};
    }

    Page page{};
    if (!manager->read_page(rid.page_id, page)) {
        return Status::io_error("failed to read RID page");
    }
    if (page.header.page_type != PageType::DATA
        || rid.slot_id >= page.slot_count()) {
        return std::optional<RowRecord>{};
    }
    std::uint16_t length = 0;
    const char* data = page.get_record(rid.slot_id, length);
    if (data == nullptr) return std::optional<RowRecord>{};
    const auto bytes = std::span(
        reinterpret_cast<const std::byte*>(data), length);
    auto decoded = RowCodec::decode(table.value().schema, bytes);
    if (!decoded.ok()) return decoded.status();
    return std::optional<RowRecord>{
        RowRecord{rid, std::move(decoded).value()}};
}

Status RealStorageAdapter::create_index(
    TableId table_id, std::string_view name,
    std::span<const std::uint32_t> key_columns) {
    (void)name;
    (void)key_columns;
    const auto table = catalog_->find_table(table_id);
    if (!table.ok()) return table.status();
    return Status::unsupported(
        "the physical hash index backend is not implemented yet");
}

Result<bool> RealStorageAdapter::has_index(
    TableId table_id, std::string_view name) const {
    (void)name;
    const auto table = catalog_->find_table(table_id);
    if (!table.ok()) return table.status();
    // Catalog state alone cannot prove that the physical backend is usable.
    // Keep this false until index_lookup has a real implementation.
    return false;
}

Result<std::vector<RID>> RealStorageAdapter::index_lookup(
    TableId table_id, std::string_view name, const Value& key) const {
    (void)name;
    (void)key;
    const auto table = catalog_->find_table(table_id);
    if (!table.ok()) return table.status();
    return Status::unsupported(
        "the physical hash index backend is not implemented yet");
}

} // namespace minidb
