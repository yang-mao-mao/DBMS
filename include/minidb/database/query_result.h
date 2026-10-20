#ifndef MINIDB_DATABASE_QUERY_RESULT_H
#define MINIDB_DATABASE_QUERY_RESULT_H

#include "minidb/common/status.h"
#include "minidb/storage/row.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace minidb {

struct QueryResult {
    Status status;
    std::vector<std::string> columns;
    std::vector<Row> rows;
    std::size_t affected_rows{0};
    std::optional<std::string> plan;

    [[nodiscard]] bool ok() const noexcept { return status.ok(); }
    [[nodiscard]] const std::string& error_message() const noexcept {
        return status.message();
    }
};

} // namespace minidb

#endif // MINIDB_DATABASE_QUERY_RESULT_H
