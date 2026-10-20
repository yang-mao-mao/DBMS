#include "minidb/catalog/catalog.h"
#include "minidb/execution/execution_engine.h"
#include "minidb/query/optimizer.h"
#include "minidb/query/planner.h"
#include "minidb/sql/parser.h"
#include "minidb/storage/table_storage.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::uint64_t sequence = 0;
        path_ = std::filesystem::temp_directory_path()
            / ("minidb_query_engine_"
               + std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count())
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

class VectorCursor final : public minidb::RowCursor {
public:
    explicit VectorCursor(const std::vector<minidb::RowRecord>& rows)
        : rows_(rows) {}

    minidb::Result<std::optional<minidb::RowRecord>> next() override {
        if (position_ >= rows_.size()) {
            return std::optional<minidb::RowRecord>{};
        }
        return std::optional<minidb::RowRecord>{rows_[position_++]};
    }

private:
    const std::vector<minidb::RowRecord>& rows_;
    std::size_t position_{0};
};

class IndexedMemoryStorage final : public minidb::ITableStorage {
public:
    explicit IndexedMemoryStorage(bool index_available = true)
        : index_available_(index_available) {
        rows_ = {
            {{1, 0}, {std::int64_t{1}, std::string{"Alice"}, std::int64_t{20}}},
            {{1, 1}, {std::int64_t{2}, std::string{"Bob"}, std::int64_t{25}}},
            {{1, 2}, {std::int64_t{3}, std::string{"Carol"}, std::int64_t{30}}},
        };
    }

    minidb::Result<minidb::TableId> create_table(
        std::string_view,
        std::span<const minidb::ColumnDefinition>) override {
        return minidb::Status::unsupported("not needed by this mock");
    }

    minidb::Result<minidb::TableSchema> get_schema(
        minidb::TableId) const override {
        return schema();
    }

    minidb::Result<minidb::RID> insert_row(
        minidb::TableId, const minidb::Row&) override {
        return minidb::Status::unsupported("not needed by this mock");
    }

    minidb::Result<std::unique_ptr<minidb::RowCursor>> scan_rows(
        minidb::TableId table_id) const override {
        if (table_id != 1) return minidb::Status::not_found("unknown table");
        ++table_scan_count;
        std::unique_ptr<minidb::RowCursor> cursor =
            std::make_unique<VectorCursor>(rows_);
        return cursor;
    }

    minidb::Result<std::optional<minidb::RowRecord>> get_row_by_rid(
        minidb::TableId table_id, minidb::RID rid) const override {
        if (table_id != 1) return minidb::Status::not_found("unknown table");
        for (const auto& row : rows_) {
            if (row.rid == rid) return std::optional<minidb::RowRecord>{row};
        }
        return std::optional<minidb::RowRecord>{};
    }

    minidb::Status create_index(
        minidb::TableId, std::string_view,
        std::span<const std::uint32_t>) override {
        return minidb::Status::unsupported("not needed by this mock");
    }

    minidb::Result<bool> has_index(
        minidb::TableId table_id, std::string_view name) const override {
        return table_id == 1 && name == "idx_students_id" && index_available_;
    }

    minidb::Result<std::vector<minidb::RID>> index_lookup(
        minidb::TableId table_id, std::string_view name,
        const minidb::Value& key) const override {
        if (table_id != 1 || name != "idx_students_id" || !index_available_) {
            return minidb::Status::unsupported("index is unavailable");
        }
        ++index_lookup_count;
        const auto* integer = std::get_if<std::int64_t>(&key);
        if (integer == nullptr) {
            return minidb::Status::invalid_argument("index key type mismatch");
        }
        std::vector<minidb::RID> matches;
        for (const auto& row : rows_) {
            if (std::get<std::int64_t>(row.row[0]) == *integer) {
                matches.push_back(row.rid);
            }
        }
        return matches;
    }

    static minidb::TableSchema schema() {
        return {
            "students",
            {
                {"id", minidb::DataType::Int64, 0, false},
                {"name", minidb::DataType::Varchar, 32, false},
                {"age", minidb::DataType::Int64, 0, false},
            },
        };
    }

    mutable std::size_t table_scan_count{0};
    mutable std::size_t index_lookup_count{0};

private:
    bool index_available_;
    std::vector<minidb::RowRecord> rows_;
};

std::unique_ptr<minidb::Catalog> make_catalog(
    const std::filesystem::path& path) {
    auto opened = minidb::Catalog::open(path);
    assert(opened.ok());
    auto catalog = std::move(opened).value();
    minidb::TableMetadata table;
    table.id = catalog->next_table_id();
    table.schema = IndexedMemoryStorage::schema();
    table.storage_file = "tables/students.mock";
    assert(catalog->register_table(table).ok());

    minidb::IndexMetadata index;
    index.id = catalog->next_index_id();
    index.table_id = table.id;
    index.name = "idx_students_id";
    index.key_columns = {0};
    index.storage_file = "indexes/students_id.mock";
    index.state = minidb::IndexState::Available;
    assert(catalog->register_index(index).ok());
    return catalog;
}

minidb::sql::SelectStatement parse_select(std::string_view text) {
    auto parsed = minidb::sql::parse_sql(text);
    assert(parsed.ok());
    auto* select = std::get_if<minidb::sql::SelectStatement>(&parsed.value());
    assert(select != nullptr);
    return *select;
}

std::unique_ptr<minidb::query::PlanNode> make_plan(
    const minidb::Catalog& catalog, std::string_view sql) {
    const auto select = parse_select(sql);
    minidb::query::QueryPlanner planner(catalog);
    auto planned = planner.plan(select);
    assert(planned.ok());
    return std::move(planned).value();
}

void test_planner_and_optimizer_index_rule() {
    TemporaryDirectory directory;
    auto catalog = make_catalog(directory.path());
    IndexedMemoryStorage storage;

    auto table_plan = make_plan(
        *catalog, "SELECT name FROM students WHERE id = 2;");
    assert(minidb::query::format_plan(*table_plan).find("TableScan")
           != std::string::npos);

    minidb::execution::ExecutionEngine engine(storage);
    auto table_rows = engine.execute(*table_plan);
    assert(table_rows.ok());
    assert(table_rows.value().size() == 1);

    auto index_plan = make_plan(
        *catalog, "SELECT name FROM students WHERE id = 2;");
    minidb::query::RuleBasedOptimizer optimizer(*catalog, storage);
    auto optimized = optimizer.optimize(std::move(index_plan));
    assert(optimized.ok());
    const std::string formatted = minidb::query::format_plan(*optimized.value());
    assert(formatted.find("IndexScan") != std::string::npos);
    assert(formatted.find("Filter(id = 2)") != std::string::npos);

    auto index_rows = engine.execute(*optimized.value());
    assert(index_rows.ok());
    assert(index_rows.value() == table_rows.value());
    assert(std::get<std::string>(index_rows.value()[0][0]) == "Bob");
    assert(storage.table_scan_count == 1);
    assert(storage.index_lookup_count == 1);
}

void test_hash_index_not_used_for_range_or_when_unavailable() {
    TemporaryDirectory directory;
    auto catalog = make_catalog(directory.path());

    IndexedMemoryStorage storage;
    auto range_plan = make_plan(
        *catalog, "SELECT name FROM students WHERE id > 1;");
    minidb::query::RuleBasedOptimizer optimizer(*catalog, storage);
    auto optimized_range = optimizer.optimize(std::move(range_plan));
    assert(optimized_range.ok());
    const std::string range_text =
        minidb::query::format_plan(*optimized_range.value());
    assert(range_text.find("TableScan") != std::string::npos);
    assert(range_text.find("IndexScan") == std::string::npos);

    auto no_where = make_plan(*catalog, "SELECT * FROM students;");
    auto optimized_no_where = optimizer.optimize(std::move(no_where));
    assert(optimized_no_where.ok());
    assert(minidb::query::format_plan(*optimized_no_where.value())
               .find("TableScan") != std::string::npos);

    IndexedMemoryStorage unavailable(false);
    minidb::query::RuleBasedOptimizer unavailable_optimizer(
        *catalog, unavailable);
    auto equality_plan = make_plan(
        *catalog, "SELECT name FROM students WHERE id = 2;");
    auto unavailable_plan =
        unavailable_optimizer.optimize(std::move(equality_plan));
    assert(unavailable_plan.ok());
    const std::string unavailable_text =
        minidb::query::format_plan(*unavailable_plan.value());
    assert(unavailable_text.find("TableScan") != std::string::npos);
    assert(unavailable_text.find("IndexScan") == std::string::npos);
}

void test_planner_semantic_validation() {
    TemporaryDirectory directory;
    auto catalog = make_catalog(directory.path());
    minidb::query::QueryPlanner planner(*catalog);

    auto select = parse_select("SELECT absent FROM students;");
    auto result = planner.plan(select);
    assert(!result.ok());
    assert(result.status().code() == minidb::ErrorCode::NotFound);

    select = parse_select("SELECT name FROM students WHERE age = 'old';");
    result = planner.plan(select);
    assert(!result.ok());
    assert(result.status().code() == minidb::ErrorCode::InvalidArgument);

    select = parse_select("SELECT * FROM absent;");
    result = planner.plan(select);
    assert(!result.ok());
    assert(result.status().code() == minidb::ErrorCode::NotFound);
}

void test_operator_lifecycle_is_explicit() {
    IndexedMemoryStorage storage;
    minidb::execution::TableScanOperator scan(storage, 1);

    auto row = scan.next();
    assert(!row.ok());
    assert(row.status().code() == minidb::ErrorCode::InternalError);
    assert(scan.open().ok());
    assert(!scan.open().ok());
    row = scan.next();
    assert(row.ok() && row.value().has_value());
    assert(scan.close().ok());
    row = scan.next();
    assert(!row.ok());
}

} // namespace

int main() {
    test_planner_and_optimizer_index_rule();
    test_hash_index_not_used_for_range_or_when_unavailable();
    test_planner_semantic_validation();
    test_operator_lifecycle_is_explicit();
}
