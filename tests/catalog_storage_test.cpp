#include "minidb/storage/real_storage_adapter.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::uint64_t sequence = 0;
        const auto timestamp = std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count();
        path_ = std::filesystem::temp_directory_path()
            / ("minidb_catalog_storage_" + std::to_string(timestamp)
               + "_" + std::to_string(sequence++));
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::vector<minidb::ColumnDefinition> user_columns() {
    return {
        {"id", minidb::DataType::Int64, 0, false},
        {"name", minidb::DataType::Varchar, 256, false},
    };
}

void test_create_insert_scan_rid_and_restart() {
    TemporaryDirectory database;
    minidb::TableId users = 0;
    minidb::RID alice_rid{};

    {
        auto opened = minidb::RealStorageAdapter::open(database.path());
        assert(opened.ok());
        auto adapter = std::move(opened).value();

        auto created = adapter->create_table("users", user_columns());
        assert(created.ok());
        users = created.value();
        assert(users != 0);
        assert(std::filesystem::exists(database.path() / "catalog.mdb"));
        assert(std::filesystem::exists(
            database.path() / "tables" /
            ("table_" + std::to_string(users) + ".mdb")));

        auto duplicate = adapter->create_table("users", user_columns());
        assert(!duplicate.ok());
        assert(duplicate.status().code() == minidb::ErrorCode::AlreadyExists);

        auto schema = adapter->get_schema(users);
        assert(schema.ok());
        assert(schema.value().name == "users");
        assert(schema.value().columns == user_columns());

        auto inserted = adapter->insert_row(
            users, minidb::Row{std::int64_t{1}, std::string("Alice")});
        assert(inserted.ok());
        alice_rid = inserted.value();
        assert(alice_rid.page_id != 0);

        inserted = adapter->insert_row(
            users, minidb::Row{std::int64_t{2}, std::string("Bob")});
        assert(inserted.ok());

        // Force the adapter to span multiple physical DATA pages.
        for (std::int64_t id = 3; id < 33; ++id) {
            auto row = adapter->insert_row(
                users, minidb::Row{id, std::string(200, 'x')});
            assert(row.ok());
        }

        auto fetched = adapter->get_row_by_rid(users, alice_rid);
        assert(fetched.ok());
        assert(fetched.value().has_value());
        assert(std::get<std::int64_t>(fetched.value()->row[0]) == 1);
        assert(std::get<std::string>(fetched.value()->row[1]) == "Alice");

        auto missing = adapter->get_row_by_rid(
            users, minidb::RID{999999, 0});
        assert(missing.ok());
        assert(!missing.value().has_value());

        auto cursor_result = adapter->scan_rows(users);
        assert(cursor_result.ok());
        auto cursor = std::move(cursor_result).value();
        std::size_t count = 0;
        while (true) {
            auto next = cursor->next();
            assert(next.ok());
            if (!next.value().has_value()) break;
            ++count;
        }
        assert(count == 32);

        auto wrong_type = adapter->insert_row(
            users, minidb::Row{std::string("bad"), std::string("value")});
        assert(!wrong_type.ok());
        assert(wrong_type.status().code()
               == minidb::ErrorCode::InvalidArgument);

        auto too_long = adapter->insert_row(
            users, minidb::Row{std::int64_t{99}, std::string(257, 'x')});
        assert(!too_long.ok());

        auto has_index = adapter->has_index(users, "idx_users_id");
        assert(has_index.ok());
        assert(!has_index.value());

        const std::vector<std::uint32_t> keys{0};
        const auto create_index = adapter->create_index(
            users, "idx_users_id", keys);
        assert(!create_index.ok());
        assert(create_index.code() == minidb::ErrorCode::Unsupported);

        auto lookup = adapter->index_lookup(
            users, "idx_users_id", minidb::Value{std::int64_t{1}});
        assert(!lookup.ok());
        assert(lookup.status().code() == minidb::ErrorCode::Unsupported);
    }

    // Catalog can persist a future physical index in Building state without
    // advertising it as usable to the planner or adapter.
    {
        auto opened_catalog = minidb::Catalog::open(database.path());
        assert(opened_catalog.ok());
        auto catalog = std::move(opened_catalog).value();
        minidb::IndexMetadata index;
        index.id = catalog->next_index_id();
        index.table_id = users;
        index.name = "idx_users_id";
        index.key_columns = {0};
        index.storage_file = "indexes/index_1.mdb";
        index.state = minidb::IndexState::Building;
        assert(catalog->register_index(index).ok());
    }

    // Reopen both catalog.mdb and the table file, then verify recovered schema
    // and data without relying on process-local metadata.
    {
        auto reopened = minidb::RealStorageAdapter::open(database.path());
        assert(reopened.ok());
        auto adapter = std::move(reopened).value();

        auto table = adapter->catalog().find_table("users");
        assert(table.ok());
        assert(table.value().id == users);
        assert(table.value().schema.columns == user_columns());

        auto fetched = adapter->get_row_by_rid(users, alice_rid);
        assert(fetched.ok() && fetched.value().has_value());
        assert(std::get<std::string>(fetched.value()->row[1]) == "Alice");

        auto cursor_result = adapter->scan_rows(users);
        assert(cursor_result.ok());
        auto cursor = std::move(cursor_result).value();
        std::size_t count = 0;
        while (true) {
            auto next = cursor->next();
            assert(next.ok());
            if (!next.value()) break;
            ++count;
        }
        assert(count == 32);

        auto index = adapter->catalog().find_index(users, "idx_users_id");
        assert(index.ok());
        assert(index.value().state == minidb::IndexState::Building);
        auto has_index = adapter->has_index(users, "idx_users_id");
        assert(has_index.ok() && !has_index.value());
    }
}

void test_invalid_schema_and_missing_table() {
    TemporaryDirectory database;
    auto opened = minidb::RealStorageAdapter::open(database.path());
    assert(opened.ok());
    auto adapter = std::move(opened).value();

    const std::vector<minidb::ColumnDefinition> invalid_columns{
        {"name", minidb::DataType::Varchar, 0, false}};
    auto created = adapter->create_table("broken", invalid_columns);
    assert(!created.ok());
    assert(created.status().code() == minidb::ErrorCode::InvalidArgument);

    auto schema = adapter->get_schema(9999);
    assert(!schema.ok());
    assert(schema.status().code() == minidb::ErrorCode::NotFound);
}

void test_missing_table_file_is_reported() {
    TemporaryDirectory database;
    std::filesystem::path table_file;
    {
        auto opened = minidb::RealStorageAdapter::open(database.path());
        assert(opened.ok());
        auto adapter = std::move(opened).value();
        auto created = adapter->create_table("users", user_columns());
        assert(created.ok());
        table_file = database.path() / "tables"
            / ("table_" + std::to_string(created.value()) + ".mdb");
    }

    std::error_code error;
    assert(std::filesystem::remove(table_file, error));
    assert(!error);
    auto reopened = minidb::RealStorageAdapter::open(database.path());
    assert(!reopened.ok());
    assert(reopened.status().code() == minidb::ErrorCode::Corruption);
}

} // namespace

int main() {
    test_create_insert_scan_rid_and_restart();
    test_invalid_schema_and_missing_table();
    test_missing_table_file_is_reported();
}
