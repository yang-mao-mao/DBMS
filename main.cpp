#include "storage/ci_pan_cun_chu.h"

#include <algorithm>
#include <iostream>
#include <string>

int main() {
    const std::string file_name = "page_test.db";
    const std::string message = "hello, database page";

    // Page{} value-initializes every byte to zero.
    // Page page{};
    // std::copy(message.begin(), message.end(), page.begin());
    //
    // auto file = open_file(file_name);
    // if (!file) {
    //     std::cerr << "failed to open " << file_name << '\n';
    //     return 1;
    // }
    //
    // if (!write_page(*file, 0, page)) {
    //     std::cerr << "failed to write page 0\n";
    //     return 1;
    // }
    //
    // // Close and reopen to prove that the page was persisted to disk.
    // file->close();
    // file = open_rw(file_name);
    // if (!file) {
    //     std::cerr << "failed to reopen " << file_name << '\n';
    //     return 1;
    // }
    //
    // Page loaded_page{};
    // if (!read_page(*file, 0, loaded_page)) {
    //     std::cerr << "failed to read page 0\n";
    //     return 1;
    // }
    //
    // if (loaded_page != page) {
    //     std::cerr << "page contents do not match\n";
    //     return 1;
    // }
    //
    // std::cout << "page 0 persisted successfully: "
    //           << std::string(loaded_page.data(), message.size()) << '\n';
    // return 0;
}
