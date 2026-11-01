#include "minidb/database/database.h"

#include "minidb/execution/execution_engine.h"
#include "minidb/query/optimizer.h"
#include "minidb/query/planner.h"
#include "minidb/sql/parser.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace minidb {
namespace {

QueryResult failed(Status status) {
    QueryResult result;
    result.status = std::move(status);
    return result;
}

Result<std::uint32_t> find_column(const TableSchema& schema,
                                  std::string_view name) {
    const auto found = std::ranges::find(schema.columns, name,
                                         &ColumnDefinition::name);
    if (found == schema.columns.end()) {
        return Status::not_found("column does not exist: " + std::string(name));
    }
    const auto index = std::distance(schema.columns.begin(), found);
    if (index < 0
        || static_cast<std::uint64_t>(index)
               > std::numeric_limits<std::uint32_t>::max()) {
        return Status::internal_error("column index is outside UINT32 range");
    }
    return static_cast<std::uint32_t>(index);
}

} // namespace

Result<std::unique_ptr<Database>> Database::open(
    const std::filesystem::path& database_directory) {
    auto storage = RealStorageAdapter::open(database_directory);
    if (!storage.ok()) return storage.status();
    return std::unique_ptr<Database>(
        new Database(std::move(storage).value()));
}

QueryResult Database::execute(const std::string& sql_text) {
    // 统一入口先解析 AST，再按语句 variant 分派；错误均转成 QueryResult。
    auto parsed = sql::parse_sql(sql_text);
    if (!parsed.ok()) return failed(parsed.status());
    const sql::Statement& statement = parsed.value();

    if (const auto* create =
            std::get_if<sql::CreateTableStatement>(&statement)) {
        return execute_create_table(*create);
    }
    if (const auto* insert = std::get_if<sql::InsertStatement>(&statement)) {
        return execute_insert(*insert);
    }
    if (const auto* select = std::get_if<sql::SelectStatement>(&statement)) {
        return execute_select(*select, false);
    }
    if (const auto* delete_statement =
            std::get_if<sql::DeleteStatement>(&statement)) {
        return execute_delete(*delete_statement);
    }
    if (const auto* create_index =
            std::get_if<sql::CreateIndexStatement>(&statement)) {
        return execute_create_index(*create_index);
    }
    const auto& explain = std::get<sql::ExplainStatement>(statement);
    if (!explain.query) {
        return failed(Status::internal_error("EXPLAIN query is missing"));
    }
    return execute_select(*explain.query, true);
}

QueryResult Database::execute_select(
    const sql::SelectStatement& statement, bool explain_only) {
    // SELECT 完整流水线：Planner -> Optimizer -> EXPLAIN 或 ExecutionEngine。
    query::QueryPlanner planner(storage_->catalog());
    auto planned = planner.plan(statement);
    if (!planned.ok()) return failed(planned.status());

    query::RuleBasedOptimizer optimizer(storage_->catalog(), *storage_);
    auto optimized = optimizer.optimize(std::move(planned).value());
    if (!optimized.ok()) return failed(optimized.status());

    QueryResult result;
    result.plan = query::format_plan(*optimized.value());
    if (explain_only) {
        result.columns = {"QUERY PLAN"};
        result.rows = {Row{*result.plan}};
        return result;
    }

    for (const auto& column : optimized.value()->output_columns()) {
        result.columns.push_back(column.name);
    }
    execution::ExecutionEngine engine(*storage_);
    auto rows = engine.execute(*optimized.value());
    if (!rows.ok()) return failed(rows.status());
    result.rows = std::move(rows).value();
    return result;
}

QueryResult Database::execute_create_table(
    const sql::CreateTableStatement& statement) {
    // DDL 不需要构造查询算子树，直接调用表级适配器。
    auto created = storage_->create_table(statement.schema.name,
                                          statement.schema.columns);
    if (!created.ok()) return failed(created.status());
    QueryResult result;
    result.affected_rows = 1;
    result.message = "Table created.";
    return result;
}

QueryResult Database::execute_insert(const sql::InsertStatement& statement) {
    auto table = storage_->catalog().find_table(statement.table_name);
    if (!table.ok()) return failed(table.status());
    auto inserted = storage_->insert_row(table.value().id, statement.values);
    if (!inserted.ok()) return failed(inserted.status());
    QueryResult result;
    result.affected_rows = 1;
    result.message = "1 row inserted.";
    return result;
}

QueryResult Database::execute_delete(const sql::DeleteStatement& statement) {
    auto table = storage_->catalog().find_table(statement.table_name);
    if (!table.ok()) return failed(table.status());

    // DELETE 复用 SELECT 的绑定、优化和 Volcano 执行路径，但投影全部字段
    // 并保留 RID，随后由 StorageAdapter 完成物理记录和索引项删除。
    sql::SelectStatement selection{
        sql::AllColumns{}, statement.table_name, statement.where};
    query::QueryPlanner planner(storage_->catalog());
    auto planned = planner.plan(selection);
    if (!planned.ok()) return failed(planned.status());
    query::RuleBasedOptimizer optimizer(storage_->catalog(), *storage_);
    auto optimized = optimizer.optimize(std::move(planned).value());
    if (!optimized.ok()) return failed(optimized.status());

    execution::ExecutionEngine engine(*storage_);
    auto records = engine.execute_records(*optimized.value());
    if (!records.ok()) return failed(records.status());

    std::size_t deleted = 0;
    for (const auto& record : records.value()) {
        const Status status = storage_->delete_row(table.value().id, record.rid);
        if (!status.ok()) return failed(status);
        ++deleted;
    }

    QueryResult result;
    result.affected_rows = deleted;
    result.message = std::to_string(deleted)
        + (deleted == 1 ? " row deleted." : " rows deleted.");
    return result;
}

QueryResult Database::execute_create_index(
    const sql::CreateIndexStatement& statement) {
    // 物理适配器只接受当前 B+ 树真实支持的单列唯一 INT64 索引。
    auto table = storage_->catalog().find_table(statement.table_name);
    if (!table.ok()) return failed(table.status());
    auto column = find_column(table.value().schema, statement.column_name);
    if (!column.ok()) return failed(column.status());
    const std::vector<std::uint32_t> key_columns{column.value()};
    const Status status = storage_->create_index(
        table.value().id, statement.index_name, key_columns);
    if (!status.ok()) return failed(status);
    QueryResult result;
    result.affected_rows = 1;
    result.message = "Index created.";
    return result;
}

} // namespace minidb
