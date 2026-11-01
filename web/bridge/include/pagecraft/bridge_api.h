#ifndef PAGECRAFT_BRIDGE_API_H
#define PAGECRAFT_BRIDGE_API_H

#include "minidb/database/database.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace pagecraft {

struct ApiResponse {
    int status{200};
    std::string body;
};

class BridgeApi {
public:
    [[nodiscard]] static minidb::Result<std::unique_ptr<BridgeApi>> open(
        const std::filesystem::path& database_directory);

    BridgeApi(const BridgeApi&) = delete;
    BridgeApi& operator=(const BridgeApi&) = delete;

    [[nodiscard]] ApiResponse handle(
        std::string_view method,
        std::string_view path,
        std::string_view body);

private:
    BridgeApi(std::filesystem::path database_directory,
              std::unique_ptr<minidb::Database> database);

    [[nodiscard]] ApiResponse health() const;
    [[nodiscard]] ApiResponse catalog() const;
    [[nodiscard]] ApiResponse query(std::string_view body);

    std::filesystem::path database_directory_;
    std::unique_ptr<minidb::Database> database_;
};

} // namespace pagecraft

#endif
