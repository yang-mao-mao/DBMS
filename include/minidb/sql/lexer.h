#ifndef MINIDB_SQL_LEXER_H
#define MINIDB_SQL_LEXER_H

#include "minidb/common/result.h"
#include "minidb/sql/token.h"

#include <string_view>
#include <vector>

namespace minidb::sql {

class Lexer {
public:
    explicit Lexer(std::string_view input) : input_(input) {}

    [[nodiscard]] Result<std::vector<Token>> tokenize();

private:
    [[nodiscard]] bool at_end() const noexcept;
    [[nodiscard]] char peek(std::size_t lookahead = 0) const noexcept;
    char advance() noexcept;
    void skip_whitespace() noexcept;
    [[nodiscard]] SourcePosition position() const noexcept;

    std::string_view input_;
    std::size_t offset_{0};
    std::size_t line_{1};
    std::size_t column_{1};
};

} // namespace minidb::sql

#endif // MINIDB_SQL_LEXER_H
