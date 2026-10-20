#include "minidb/database/database.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::uint64_t sequence = 0;
        const auto timestamp = std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count();
        path_ = std::filesystem::temp_directory_path()
            / ("minidb_database_test_" + std::to_string(timestamp)
               + "_" + std::to_string(sequence++));
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void assert_ok(const minidb::QueryResult& result) {
    assert(result.ok());
    assert(result.error_message().empty());
}

void test_database_end_to_end() {
    TemporaryDirectory directory;
    auto opened = minidb::Database::open(directory.path());
    assert(opened.ok());
    auto database = std::move(opened).value();

    auto result = database->execute(
        "CREATE TABLE students (id INT, name VARCHAR(32), age INT);");
    assert_ok(result);
    assert(result.affected_rows == 1);

    assert_ok(database->execute(
        "INSERT INTO students VALUES (1, 'Alice', 20);"));
    assert_ok(database->execute(
        "INSERT INTO students VALUES (2, 'Bob', 25);"));
    assert_ok(database->execute(
        "INSERT INTO students VALUES (3, 'Carol', 30);"));

    result = database->execute("SELECT * FROM students;");
    assert_ok(result);
    assert((result.columns == std::vector<std::string>{"id", "name", "age"}));
    assert(result.rows.size() == 3);
    assert(result.plan.has_value());
    assert(result.plan->find("TableScan(table=students)") != std::string::npos);
    assert(result.plan->find("Filter") == std::string::npos);
    assert(std::get<std::string>(result.rows[1][1]) == "Bob");

    result = database->execute(
        "SELECT name FROM students WHERE age > 20;");
    assert_ok(result);
    assert((result.columns == std::vector<std::string>{"name"}));
    assert(result.rows.size() == 2);
    assert(std::get<std::string>(result.rows[0][0]) == "Bob");
    assert(std::get<std::string>(result.rows[1][0]) == "Carol");
    assert(result.plan->find("Project(name)") != std::string::npos);
    assert(result.plan->find("Filter(age > 20)") != std::string::npos);
    assert(result.plan->find("TableScan") != std::string::npos);

    result = database->execute(
        "SELECT id FROM students WHERE name = 'Bob';");
    assert_ok(result);
    assert(result.rows.size() == 1);
    assert(std::get<std::int64_t>(result.rows[0][0]) == 2);

    result = database->execute(
        "SELECT name FROM students WHERE age > 100;");
    assert_ok(result);
    assert(result.rows.empty());

    result = database->execute(
        "EXPLAIN SELECT name FROM students WHERE id = 1;");
    assert_ok(result);
    assert((result.columns == std::vector<std::string>{"QUERY PLAN"}));
    assert(result.rows.size() == 1);
    assert(result.plan.has_value());
    assert(std::get<std::string>(result.rows[0][0]) == *result.plan);
    assert(result.plan->find("TableScan") != std::string::npos);
    assert(result.affected_rows == 0);

    result = database->execute("SELECT * FROM missing;");
    assert(!result.ok());
    assert(result.status.code() == minidb::ErrorCode::NotFound);

    result = database->execute("SELECT missing FROM students;");
    assert(!result.ok());
    assert(result.status.code() == minidb::ErrorCode::NotFound);

    result = database->execute(
        "SELECT name FROM students WHERE missing = 1;");
    assert(!result.ok());
    assert(result.status.code() == minidb::ErrorCode::NotFound);

    result = database->execute(
        "SELECT name FROM students WHERE age = 'twenty';");
    assert(!result.ok());
    assert(result.status.code() == minidb::ErrorCode::InvalidArgument);
    assert(result.error_message().find("type") != std::string::npos);

    result = database->execute(
        "INSERT INTO students VALUES ('wrong', 'Dave', 40);");
    assert(!result.ok());
    assert(result.status.code() == minidb::ErrorCode::InvalidArgument);

    result = database->execute(
        "CREATE INDEX idx_students_id ON students(id);");
    assert(!result.ok());
    assert(result.status.code() == minidb::ErrorCode::Unsupported);
}

void test_database_restart() {
    TemporaryDirectory directory;
    {
        auto opened = minidb::Database::open(directory.path());
        assert(opened.ok());
        auto database = std::move(opened).value();
        assert_ok(database->execute("CREATE TABLE t (id INT);"));
        assert_ok(database->execute("INSERT INTO t VALUES (42);"));
    }
    auto reopened = minidb::Database::open(directory.path());
    assert(reopened.ok());
    auto database = std::move(reopened).value();
    const auto result = database->execute("SELECT id FROM t;");
    assert_ok(result);
    assert(result.rows.size() == 1);
    assert(std::get<std::int64_t>(result.rows[0][0]) == 42);
}

void test_catalog_metadata_cannot_fake_a_physical_index() {
    TemporaryDirectory directory;
    minidb::TableId table_id = 0;
    {
        auto opened = minidb::RealStorageAdapter::open(directory.path());
        assert(opened.ok());
        auto storage = std::move(opened).value();
        const std::vector<minidb::ColumnDefinition> columns{
            {"id", minidb::DataType::Int64, 0, false}};
        auto created = storage->create_table("indexed", columns);
        assert(created.ok());
        table_id = created.value();
    }
    {
        auto opened = minidb::Catalog::open(directory.path());
        assert(opened.ok());
        auto catalog = std::move(opened).value();
        minidb::IndexMetadata index;
        index.id = catalog->next_index_id();
        index.table_id = table_id;
        index.name = "idx_id";
        index.key_columns = {0};
        index.storage_file = "indexes/index_1.mdb";
        index.state = minidb::IndexState::Available;
        assert(catalog->register_index(index).ok());
    }
    auto reopened = minidb::RealStorageAdapter::open(directory.path());
    assert(reopened.ok());
    auto storage = std::move(reopened).value();
    auto available = storage->has_index(table_id, "idx_id");
    assert(available.ok());
    assert(!available.value());
}

} // namespace

int main() {
    test_database_end_to_end();
    test_database_restart();
    test_catalog_metadata_cannot_fake_a_physical_index();
}
