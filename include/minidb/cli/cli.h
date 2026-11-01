#ifndef MINIDB_CLI_CLI_H
#define MINIDB_CLI_CLI_H

#include "minidb/database/database.h"
#include "minidb/database/query_result.h"

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace minidb::cli {

// 累积多行输入，并只把“字符串之外的分号”视为语句结束符。
// SQL 单引号以两个连续单引号转义，因此需要在字符级维护字符串状态。
class SqlStatementBuffer {
public:
    [[nodiscard]] std::vector<std::string> append_line(std::string_view line);
    [[nodiscard]] bool has_pending_input() const noexcept;
    [[nodiscard]] bool inside_string() const noexcept { return inside_string_; }
    void clear() noexcept;

private:
    std::string buffer_;
    bool inside_string_{false};
};

// 交互式终端前端。CLI 只负责输入/显示，SQL 语义统一交给 Database::execute。
class Cli {
public:
    Cli(Database& database, std::istream& input, std::ostream& output,
        std::ostream& error)
        : database_(database), input_(input), output_(output), error_(error) {}

    int run();

    static void print_result(const QueryResult& result, std::ostream& output,
                             std::ostream& error);
    static void print_help(std::ostream& output);

private:
    Database& database_;
    std::istream& input_;
    std::ostream& output_;
    std::ostream& error_;
};

} // namespace minidb::cli

#endif // MINIDB_CLI_CLI_H
