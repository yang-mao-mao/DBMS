#include "minidb/cli/cli.h"
#include "minidb/database/database.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: minidb [database_directory]\n";
        return 2;
    }

    const std::filesystem::path database_directory =
        argc == 2 ? std::filesystem::path(argv[1])
                  : std::filesystem::path("minidb_data");
    auto opened = minidb::Database::open(database_directory);
    if (!opened.ok()) {
        std::cerr << "Failed to open MiniDB: " << opened.status().message()
                  << '\n';
        return 1;
    }

    auto database = std::move(opened).value();
    minidb::cli::Cli cli(*database, std::cin, std::cout, std::cerr);
    return cli.run();
}
