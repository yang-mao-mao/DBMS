#ifndef MINIDB_CLI_CLI_H
#define MINIDB_CLI_CLI_H

#include "minidb/database/database.h"
#include "minidb/database/query_result.h"

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace minidb::cli {

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
