#include "minidb/cli/cli.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace minidb::cli {
namespace {

bool has_non_whitespace(std::string_view text) {
    return std::ranges::any_of(text, [](char character) {
        return std::isspace(static_cast<unsigned char>(character)) == 0;
    });
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size()
           && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin
           && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string value_text(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return "NULL";
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*integer);
    }
    return std::get<std::string>(value);
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find('\n', begin);
        if (end == std::string::npos) {
            lines.push_back(text.substr(begin));
            break;
        }
        lines.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return lines;
}

void print_border(const std::vector<std::size_t>& widths,
                  std::ostream& output) {
    output << '+';
    for (const std::size_t width : widths) {
        output << std::string(width + 2, '-') << '+';
    }
    output << '\n';
}

void print_cells(const std::vector<std::string>& cells,
                 const std::vector<std::size_t>& widths,
                 std::ostream& output) {
    std::vector<std::vector<std::string>> lines;
    lines.reserve(cells.size());
    std::size_t height = 1;
    for (const auto& cell : cells) {
        lines.push_back(split_lines(cell));
        height = std::max(height, lines.back().size());
    }

    for (std::size_t row_line = 0; row_line < height; ++row_line) {
        output << '|';
        for (std::size_t column = 0; column < widths.size(); ++column) {
            const std::string_view content =
                row_line < lines[column].size()
                    ? std::string_view(lines[column][row_line])
                    : std::string_view{};
            output << ' ' << std::left << std::setw(
                static_cast<int>(widths[column])) << content << " |";
        }
        output << '\n';
    }
}

void print_table(const QueryResult& result, std::ostream& output) {
    std::vector<std::vector<std::string>> rows;
    rows.reserve(result.rows.size());
    std::vector<std::size_t> widths;
    widths.reserve(result.columns.size());
    for (const auto& column : result.columns) {
        std::size_t width = 0;
        for (const auto& line : split_lines(column)) {
            width = std::max(width, line.size());
        }
        widths.push_back(width);
    }

    for (const auto& row : result.rows) {
        std::vector<std::string> cells;
        cells.reserve(result.columns.size());
        for (std::size_t column = 0; column < result.columns.size(); ++column) {
            cells.push_back(column < row.size() ? value_text(row[column]) : "NULL");
            for (const auto& line : split_lines(cells.back())) {
                widths[column] = std::max(widths[column], line.size());
            }
        }
        rows.push_back(std::move(cells));
    }

    print_border(widths, output);
    print_cells(result.columns, widths, output);
    print_border(widths, output);
    for (const auto& row : rows) print_cells(row, widths, output);
    print_border(widths, output);
    output << result.rows.size() << (result.rows.size() == 1 ? " row." : " rows.")
           << '\n';
}

} // namespace

std::vector<std::string> SqlStatementBuffer::append_line(
    std::string_view line) {
    std::vector<std::string> statements;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char character = line[i];
        buffer_.push_back(character);
        if (character == '\'') {
            if (inside_string_ && i + 1 < line.size() && line[i + 1] == '\'') {
                buffer_.push_back(line[++i]);
            } else {
                inside_string_ = !inside_string_;
            }
        } else if (character == ';' && !inside_string_) {
            if (has_non_whitespace(buffer_)) {
                statements.push_back(trim(buffer_));
            }
            buffer_.clear();
        }
    }

    if (has_non_whitespace(buffer_) || inside_string_) {
        buffer_.push_back('\n');
    } else {
        buffer_.clear();
    }
    return statements;
}

bool SqlStatementBuffer::has_pending_input() const noexcept {
    return inside_string_ || has_non_whitespace(buffer_);
}

void SqlStatementBuffer::clear() noexcept {
    buffer_.clear();
    inside_string_ = false;
}

void Cli::print_result(const QueryResult& result, std::ostream& output,
                       std::ostream& error) {
    if (!result.ok()) {
        error << "Error: " << result.error_message() << '\n';
        return;
    }
    if (!result.columns.empty()) print_table(result, output);
    if (!result.message.empty()) output << result.message << '\n';
}

void Cli::print_help(std::ostream& output) {
    output
        << "MiniDB commands:\n"
        << "  .help  Show this help.\n"
        << "  .exit  Exit MiniDB.\n"
        << "  .quit  Exit MiniDB.\n"
        << "Terminate SQL statements with a semicolon. Multi-line SQL is supported.\n";
}

int Cli::run() {
    output_ << "Welcome to MiniDB!\n\n";
    SqlStatementBuffer statements;
    std::string line;

    while (true) {
        output_ << (statements.has_pending_input() ? "   ...> " : "MiniDB> ")
                << std::flush;
        if (!std::getline(input_, line)) {
            if (statements.has_pending_input()) {
                error_ << "Error: incomplete SQL statement at end of input.\n";
            }
            output_ << "Bye!\n";
            return 0;
        }

        const std::string command = trim(line);
        if (!statements.has_pending_input() && !command.empty()
            && command.front() == '.') {
            if (command == ".exit" || command == ".quit") {
                output_ << "Bye!\n";
                return 0;
            }
            if (command == ".help") {
                print_help(output_);
            } else {
                error_ << "Error: unknown command: " << command << '\n';
            }
            continue;
        }

        for (const auto& statement : statements.append_line(line)) {
            print_result(database_.execute(statement), output_, error_);
        }
    }
}

} // namespace minidb::cli
