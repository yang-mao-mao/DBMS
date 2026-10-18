#include "ci_pan_cun_chu.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace {

class TemporaryDatabaseFile {
public:
    TemporaryDatabaseFile() {
        static std::uint64_t sequence = 0;
        const auto timestamp = std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count();
        path_ = std::filesystem::temp_directory_path()
            / ("db_test_disk_page_" + std::to_string(timestamp)
               + "_" + std::to_string(sequence++) + ".db");
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    ~TemporaryDatabaseFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    TemporaryDatabaseFile(const TemporaryDatabaseFile&) = delete;
    TemporaryDatabaseFile& operator=(const TemporaryDatabaseFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

Page make_page(std::uint32_t page_id, const std::string& value) {
    Page page{};
    page.init(page_id, PageType::DATA);
    assert(page.insert_record(value.data(),
                              static_cast<std::uint16_t>(value.size())) == 0);
    return page;
}

std::string first_record(const Page& page) {
    std::uint16_t length = 0;
    const char* record = page.get_record(0, length);
    assert(record != nullptr);
    return std::string(record, length);
}

bool same_bytes(const Page& left, const Page& right) {
    return std::memcmp(&left, &right, sizeof(Page)) == 0;
}

void test_persist_and_read_multiple_pages() {
    TemporaryDatabaseFile database;
    const Page page0 = make_page(0, "page zero");
    const Page page1 = make_page(1, "page one");
    const Page page2 = make_page(2, "page two");

    {
        auto file = create_rw(database.path().string());
        assert(file != nullptr);
        assert(write_page(*file, 0, page0));
        assert(write_page(*file, 1, page1));
        assert(write_page(*file, 2, page2));
    }

    assert(std::filesystem::file_size(database.path()) == 3 * PAGE_SIZE);

    auto file = open_rw(database.path().string());
    assert(file != nullptr);
    Page loaded{};
    assert(read_page(*file, 2, loaded));
    assert(first_record(loaded) == "page two");
    assert(read_page(*file, 0, loaded));
    assert(first_record(loaded) == "page zero");
    assert(read_page(*file, 1, loaded));
    assert(first_record(loaded) == "page one");
}

void test_read_failure_preserves_output() {
    TemporaryDatabaseFile database;
    const Page stored = make_page(0, "only page");
    {
        auto file = create_rw(database.path().string());
        assert(file != nullptr);
        assert(write_page(*file, 0, stored));
    }

    Page output = make_page(77, "sentinel");
    const Page before = output;
    auto file = open_rw(database.path().string());
    assert(file != nullptr);
    assert(!read_page(*file, 1, output));
    assert(same_bytes(output, before));
}

void test_short_read_preserves_output() {
    TemporaryDatabaseFile database;
    {
        std::ofstream file(database.path(), std::ios::binary | std::ios::trunc);
        assert(file.is_open());
        const std::array<char, 1000> partial{};
        file.write(partial.data(), static_cast<std::streamsize>(partial.size()));
        assert(file.good());
    }

    Page output = make_page(88, "unchanged");
    const Page before = output;
    auto file = open_rw(database.path().string());
    assert(file != nullptr);
    assert(!read_page(*file, 0, output));
    assert(same_bytes(output, before));
}

void test_corrupted_page_is_rejected() {
    TemporaryDatabaseFile database;
    const Page stored = make_page(0, "checksum protected");
    {
        auto file = create_rw(database.path().string());
        assert(file != nullptr);
        assert(write_page(*file, 0, stored));
    }

    {
        std::fstream file(database.path(),
                          std::ios::binary | std::ios::in | std::ios::out);
        assert(file.is_open());
        file.seekg(static_cast<std::streamoff>(PAGE_SIZE - 1), std::ios::beg);
        assert(file.good());
        char byte = 0;
        file.read(&byte, 1);
        file.clear();
        file.seekp(static_cast<std::streamoff>(PAGE_SIZE - 1), std::ios::beg);
        byte ^= 1;
        file.write(&byte, 1);
        file.flush();
        assert(file.good());
    }

    Page output = make_page(89, "sentinel");
    const Page before = output;
    auto file = open_rw(database.path().string());
    assert(file != nullptr);
    assert(!read_page(*file, 0, output));
    assert(same_bytes(output, before));
}

void test_write_rejects_invalid_page_identity() {
    TemporaryDatabaseFile database;
    auto file = create_rw(database.path().string());
    assert(file != nullptr);

    Page wrong_id{};
    wrong_id.init(3, PageType::DATA);
    assert(!write_page(*file, 4, wrong_id));

    Page stale_checksum{};
    stale_checksum.init(0, PageType::DATA);
    stale_checksum.header.next_page = 123;
    assert(!write_page(*file, 0, stale_checksum));

    Page wrong_magic{};
    wrong_magic.init(0, PageType::DATA);
    wrong_magic.header.magic = 0;
    wrong_magic.update_checksum();
    assert(!write_page(*file, 0, wrong_magic));

    Page wrong_size{};
    wrong_size.init(0, PageType::DATA);
    wrong_size.header.page_size = 1024;
    wrong_size.update_checksum();
    assert(!write_page(*file, 0, wrong_size));

    Page wrong_version{};
    wrong_version.init(0, PageType::DATA);
    wrong_version.header.file_version = PAGE_FORMAT_VERSION + 1;
    wrong_version.update_checksum();
    assert(!write_page(*file, 0, wrong_version));

    Page wrong_type{};
    wrong_type.init(0, PageType::DATA);
    wrong_type.header.page_type = static_cast<PageType>(999);
    wrong_type.update_checksum();
    assert(!write_page(*file, 0, wrong_type));

    Page invalid_slot{};
    invalid_slot.init(0, PageType::DATA);
    assert(invalid_slot.insert_record("record", 6) == 0);
    Slot out_of_bounds{static_cast<std::uint16_t>(PAGE_SIZE - 1), 8, 0};
    std::memcpy(reinterpret_cast<char*>(&invalid_slot) + sizeof(PageHeader),
                &out_of_bounds, sizeof(out_of_bounds));
    invalid_slot.update_checksum();
    assert(!write_page(*file, 0, invalid_slot));
}

void test_read_rejects_wrong_page_id() {
    TemporaryDatabaseFile database;
    const Page wrong_id = make_page(1, "stored at the wrong offset");
    {
        std::ofstream file(database.path(), std::ios::binary | std::ios::trunc);
        assert(file.is_open());
        file.write(reinterpret_cast<const char*>(&wrong_id),
                   static_cast<std::streamsize>(sizeof(Page)));
        assert(file.good());
    }

    Page output = make_page(90, "sentinel");
    const Page before = output;
    auto file = open_rw(database.path().string());
    assert(file != nullptr);
    assert(!read_page(*file, 0, output));
    assert(same_bytes(output, before));
}

void test_closed_file_and_overflow_are_rejected() {
    TemporaryDatabaseFile database;
    Page page = make_page(0, "data");
    std::fstream closed;
    assert(!write_page(closed, 0, page));
    assert(!read_page(closed, 0, page));

    auto file = create_rw(database.path().string());
    assert(file != nullptr);
    const auto impossible_page = std::numeric_limits<std::uint64_t>::max();
    assert(!write_page(*file, impossible_page, page));
    assert(!read_page(*file, impossible_page, page));
}

} // namespace

int main() {
    test_persist_and_read_multiple_pages();
    test_read_failure_preserves_output();
    test_short_read_preserves_output();
    test_corrupted_page_is_rejected();
    test_write_rejects_invalid_page_identity();
    test_read_rejects_wrong_page_id();
    test_closed_file_and_overflow_are_rejected();
}
