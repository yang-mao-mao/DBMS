#ifndef MINIDB_DATABASE_DATABASE_H
#define MINIDB_DATABASE_DATABASE_H

#include "minidb/common/result.h"
#include "minidb/database/query_result.h"
#include "minidb/query/plan.h"
#include "minidb/sql/ast.h"
#include "minidb/storage/real_storage_adapter.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace minidb {

class Database {
public:
    [[nodiscard]] static Result<std::unique_ptr<Database>> open(
        const std::filesystem::path& database_directory);

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    [[nodiscard]] QueryResult execute(const std::string& sql_text);

private:
    explicit Database(std::unique_ptr<RealStorageAdapter> storage)
        : storage_(std::move(storage)) {}

    [[nodiscard]] QueryResult execute_select(
        const sql::SelectStatement& statement, bool explain_only);
    [[nodiscard]] QueryResult execute_create_table(
        const sql::CreateTableStatement& statement);
    [[nodiscard]] QueryResult execute_insert(
        const sql::InsertStatement& statement);
    [[nodiscard]] QueryResult execute_create_index(
        const sql::CreateIndexStatement& statement);

    std::unique_ptr<RealStorageAdapter> storage_;
};

} // namespace minidb

#endif // MINIDB_DATABASE_DATABASE_H
