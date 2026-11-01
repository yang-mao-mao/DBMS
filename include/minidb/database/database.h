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

// MiniDB 上层门面：串联 Parser、Planner、Optimizer、ExecutionEngine 和存储适配层。
// 当前采用单线程、单语句自动提交模型。
class Database {
public:
    // 打开指定数据库目录，并恢复 Catalog；返回对象独占真实存储适配器。
    [[nodiscard]] static Result<std::unique_ptr<Database>> open(
        const std::filesystem::path& database_directory);

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // 解析并执行一条 SQL。语法或运行错误通过 QueryResult 返回，不使 CLI 退出。
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
    [[nodiscard]] QueryResult execute_delete(
        const sql::DeleteStatement& statement);
    [[nodiscard]] QueryResult execute_create_index(
        const sql::CreateIndexStatement& statement);

    std::unique_ptr<RealStorageAdapter> storage_;
};

} // namespace minidb

#endif // MINIDB_DATABASE_DATABASE_H
