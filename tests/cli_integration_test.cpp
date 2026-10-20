#include "minidb/cli/cli.h"
#include "minidb/database/database.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::uint64_t sequence = 0;
        path_ = std::filesystem::temp_directory_path()
            / ("minidb_cli_integration_"
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

struct CliOutput {
    int exit_code{0};
    std::string standard_output;
    std::string standard_error;
};

CliOutput run_cli(minidb::Database& database, const std::string& input_text) {
    std::istringstream input(input_text);
    std::ostringstream output;
    std::ostringstream error;
    minidb::cli::Cli cli(database, input, output, error);
    const int exit_code = cli.run();
    return {exit_code, output.str(), error.str()};
}

void test_multiline_sql_table_output_errors_and_explain() {
    TemporaryDirectory directory;
    auto opened = minidb::Database::open(directory.path());
    assert(opened.ok());
    auto database = std::move(opened).value();

    const std::string script =
        ".help\n"
        "CREATE TABLE students (\n"
        "  id INT,\n"
        "  name VARCHAR(32)\n"
        ");\n"
        "INSERT INTO students VALUES (1, 'Alice; A');\n"
        "INSERT INTO students VALUES (2, 'Bob');\n"
        "SELECT * FROM students;\n"
        "SELECT name\n"
        "FROM students\n"
        "WHERE id > 1;\n"
        "CREATE INDEX idx_students_id ON students(id);\n"
        "SELECT name FROM students WHERE id = 1;\n"
        "EXPLAIN SELECT name FROM students WHERE id = 1;\n"
        "SELEC broken;\n"
        "SELECT name FROM students WHERE id = 2;\n"
        ".quit\n";

    const CliOutput result = run_cli(*database, script);
    assert(result.exit_code == 0);
    assert(result.standard_output.find("Welcome to MiniDB!")
           != std::string::npos);
    assert(result.standard_output.find("MiniDB commands:")
           != std::string::npos);
    assert(result.standard_output.find("Table created.") != std::string::npos);
    assert(result.standard_output.find("1 row inserted.") != std::string::npos);
    assert(result.standard_output.find("| id | name     |")
           != std::string::npos);
    assert(result.standard_output.find("| 1  | Alice; A |")
           != std::string::npos);
    assert(result.standard_output.find("| 2  | Bob      |")
           != std::string::npos);
    assert(result.standard_output.find("Filter(id = 1)")
           != std::string::npos);
    assert(result.standard_output.find("TableScan(table=students)")
           != std::string::npos);
    assert(result.standard_output.find("IndexScan") == std::string::npos);
    assert(result.standard_output.find("Bye!") != std::string::npos);

    assert(result.standard_error.find(
               "physical hash index backend is not implemented")
           != std::string::npos);
    assert(result.standard_error.find("unsupported or missing SQL statement")
           != std::string::npos);

    // The valid SELECT after the malformed statement proves that an SQL error
    // does not terminate the CLI loop.
    const std::size_t syntax_error = result.standard_error.find("unsupported");
    assert(syntax_error != std::string::npos);
    assert(result.standard_output.rfind("| Bob  |") != std::string::npos);
}

void test_restart_recovers_catalog_and_rows() {
    TemporaryDirectory directory;
    {
        auto opened = minidb::Database::open(directory.path());
        assert(opened.ok());
        auto database = std::move(opened).value();
        const CliOutput first = run_cli(
            *database,
            "CREATE TABLE persistent (id INT, value VARCHAR(16));\n"
            "INSERT INTO persistent VALUES (7, 'saved');\n"
            ".exit\n");
        assert(first.exit_code == 0);
        assert(first.standard_error.empty());
    }

    assert(std::filesystem::exists(directory.path() / "catalog.mdb"));
    assert(std::filesystem::exists(
        directory.path() / "tables" / "table_1.mdb"));

    auto reopened = minidb::Database::open(directory.path());
    assert(reopened.ok());
    auto database = std::move(reopened).value();
    const CliOutput second = run_cli(
        *database, "SELECT * FROM persistent;\n.exit\n");
    assert(second.exit_code == 0);
    assert(second.standard_error.empty());
    assert(second.standard_output.find("| 7  | saved |")
           != std::string::npos);
}

void test_meta_commands_eof_and_statement_buffer() {
    TemporaryDirectory directory;
    auto opened = minidb::Database::open(directory.path());
    assert(opened.ok());
    auto database = std::move(opened).value();

    CliOutput result = run_cli(*database, ".unknown\n.exit\n");
    assert(result.standard_error.find("unknown command: .unknown")
           != std::string::npos);

    result = run_cli(*database, "SELECT * FROM\n");
    assert(result.exit_code == 0);
    assert(result.standard_error.find("incomplete SQL statement")
           != std::string::npos);

    minidb::cli::SqlStatementBuffer buffer;
    auto complete = buffer.append_line(
        "INSERT INTO t VALUES (1, 'semicolon; and ''quote'''); SELECT * FROM t;");
    assert(complete.size() == 2);
    assert(complete[0].find("semicolon; and ''quote''") != std::string::npos);
    assert(!buffer.has_pending_input());
}

} // namespace

int main() {
    test_multiline_sql_table_output_errors_and_explain();
    test_restart_recovers_catalog_and_rows();
    test_meta_commands_eof_and_statement_buffer();
}
