#include "DiskManager.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
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
            / ("db_test_manager_" + std::to_string(timestamp)
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

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::string first_record(const Page& page) {
    std::uint16_t length = 0;
    const char* data = page.get_record(0, length);
    if (data == nullptr) return {};
    return std::string(data, length);
}

void test_create_allocate_write_and_reopen() {
    TemporaryDatabaseFile database;
    std::uint32_t first = INVALID_PAGE;
    std::uint32_t second = INVALID_PAGE;

    {
        auto manager = DiskManager::open(database.path().string());
        assert(manager != nullptr);
        assert(manager->page_count() == 1);
        assert(manager->allocate_page(PageType::META) == INVALID_PAGE);
        assert(manager->allocate_page(PageType::FREE) == INVALID_PAGE);
        assert(manager->allocate_page(PageType::INVALID) == INVALID_PAGE);
        assert(manager->page_count() == 1);

        first = manager->allocate_page();
        second = manager->allocate_page(PageType::INDEX);
        assert(first == 1);
        assert(second == 2);
        assert(manager->page_count() == 3);

        Page page{};
        assert(manager->read_page(first, page));
        assert(page.header.page_type == PageType::DATA);
        assert(page.insert_record("persistent", 10) == 0);
        assert(manager->write_page(first, page));
        assert(manager->flush());
    }

    {
        auto manager = DiskManager::open(database.path().string());
        assert(manager != nullptr);
        assert(manager->page_count() == 3);

        Page page{};
        assert(manager->read_page(first, page));
        assert(first_record(page) == "persistent");
        assert(page.header.page_type == PageType::DATA);

        assert(manager->read_page(second, page));
        assert(page.header.page_type == PageType::INDEX);
    }
}

void test_free_page_reuse_clears_old_contents() {
    TemporaryDatabaseFile database;
    std::uint32_t page_no = INVALID_PAGE;
    {
        auto manager = DiskManager::open(database.path().string());
        assert(manager != nullptr);
        page_no = manager->allocate_page();
        assert(page_no == 1);
        Page page{};
        assert(manager->read_page(page_no, page));
        assert(page.insert_record("secret old data", 15) == 0);
        assert(manager->write_page(page_no, page));

        assert(manager->free_page(page_no));
        assert(!manager->free_page(page_no));
        assert(manager->read_page(page_no, page));
        assert(page.header.page_type == PageType::FREE);
        assert(page.slot_count() == 0);
    }

    auto manager = DiskManager::open(database.path().string());
    assert(manager != nullptr);
    const std::uint32_t reused = manager->allocate_page(PageType::OVERFLOW);
    assert(reused == page_no);
    Page page{};
    assert(manager->read_page(reused, page));
    assert(page.header.page_type == PageType::OVERFLOW);
    assert(page.slot_count() == 0);
    std::uint16_t length = 0;
    assert(page.get_record(0, length) == nullptr);
    assert(length == 0);
    assert(page.verify_checksum());
}

void test_recovers_uncommitted_free_tail_page() {
    TemporaryDatabaseFile database;
    {
        auto manager = DiskManager::open(database.path().string());
        assert(manager != nullptr);
        assert(manager->page_count() == 1);
    }

    {
        std::fstream file(database.path(),
                          std::ios::binary | std::ios::in | std::ios::out);
        assert(file.is_open());
        Page orphan{};
        orphan.init(1, PageType::FREE);
        assert(write_page(file, 1, orphan));
    }

    auto manager = DiskManager::open(database.path().string());
    assert(manager != nullptr);
    assert(manager->page_count() == 2);
    assert(manager->allocate_page(PageType::INDEX) == 1);
    Page allocated{};
    assert(manager->read_page(1, allocated));
    assert(allocated.header.page_type == PageType::INDEX);
}

void test_rejects_partial_file_and_meta_count_mismatch() {
    {
        TemporaryDatabaseFile database;
        std::ofstream file(database.path(), std::ios::binary | std::ios::trunc);
        assert(file.is_open());
        const char partial[] = "bad";
        file.write(partial, sizeof(partial));
        assert(file.good());
        file.close();
        assert(DiskManager::open(database.path().string()) == nullptr);
    }

    {
        TemporaryDatabaseFile database;
        {
            auto manager = DiskManager::open(database.path().string());
            assert(manager != nullptr);
            assert(manager->allocate_page() == 1);
        }
        {
            std::ofstream file(database.path(), std::ios::binary | std::ios::app);
            assert(file.is_open());
            std::string extra(PAGE_SIZE, '\0');
            file.write(extra.data(), static_cast<std::streamsize>(extra.size()));
            assert(file.good());
        }
        assert(DiskManager::open(database.path().string()) == nullptr);
    }
}

void test_rejects_corrupted_meta_page() {
    TemporaryDatabaseFile database;
    {
        auto manager = DiskManager::open(database.path().string());
        assert(manager != nullptr);
    }

    {
        std::fstream file(database.path(),
                          std::ios::binary | std::ios::in | std::ios::out);
        assert(file.is_open());
        file.seekg(static_cast<std::streamoff>(sizeof(PageHeader)), std::ios::beg);
        char byte = 0;
        file.read(&byte, 1);
        assert(file.gcount() == 1);
        file.clear();
        file.seekp(static_cast<std::streamoff>(sizeof(PageHeader)), std::ios::beg);
        byte ^= 1;
        file.write(&byte, 1);
        file.flush();
        assert(file.good());
    }

    assert(DiskManager::open(database.path().string()) == nullptr);
}

void test_repairs_corrupt_freelist_cycle() {
    TemporaryDatabaseFile database;
    std::uint32_t free_page = INVALID_PAGE;
    {
        auto manager = DiskManager::open(database.path().string());
        assert(manager != nullptr);
        free_page = manager->allocate_page();
        assert(free_page == 1);
        assert(manager->free_page(free_page));
    }

    {
        std::fstream file(database.path(),
                          std::ios::binary | std::ios::in | std::ios::out);
        assert(file.is_open());

        Page page{};
        assert(read_page(file, free_page, page));
        page.header.next_page = free_page;
        page.update_checksum();
        assert(write_page(file, free_page, page));
    }

    auto manager = DiskManager::open(database.path().string());
    assert(manager != nullptr);
    assert(manager->allocate_page() == free_page);
    assert(manager->allocate_page() == 2);
}

void test_recovers_orphan_free_page_inside_file() {
    TemporaryDatabaseFile database;
    std::uint32_t free_page = INVALID_PAGE;
    {
        auto manager = DiskManager::open(database.path().string());
        assert(manager != nullptr);
        free_page = manager->allocate_page();
        assert(free_page == 1);
        assert(manager->free_page(free_page));
    }

    {
        std::fstream file(database.path(),
                          std::ios::binary | std::ios::in | std::ios::out);
        assert(file.is_open());
        Page meta{};
        assert(read_page(file, 0, meta));
        const std::uint32_t no_free_pages = INVALID_PAGE;
        std::memcpy(meta.body + 2 * sizeof(std::uint32_t),
                    &no_free_pages, sizeof(no_free_pages));
        meta.update_checksum();
        assert(write_page(file, 0, meta));
    }

    auto manager = DiskManager::open(database.path().string());
    assert(manager != nullptr);
    assert(manager->allocate_page(PageType::INDEX) == free_page);
}

void test_rejects_freeing_meta_and_writing_free_pages() {
    TemporaryDatabaseFile database;
    auto manager = DiskManager::open(database.path().string());
    assert(manager != nullptr);
    assert(!manager->free_page(0));

    const std::uint32_t page_no = manager->allocate_page();
    assert(page_no == 1);
    assert(manager->free_page(page_no));

    Page free{};
    free.init(page_no, PageType::FREE);
    assert(!manager->write_page(page_no, free));
    assert(!manager->write_page(0, free));
}

} // namespace

int main() {
    test_create_allocate_write_and_reopen();
    test_free_page_reuse_clears_old_contents();
    test_recovers_uncommitted_free_tail_page();
    test_rejects_partial_file_and_meta_count_mismatch();
    test_rejects_corrupted_meta_page();
    test_repairs_corrupt_freelist_cycle();
    test_recovers_orphan_free_page_inside_file();
    test_rejects_freeing_meta_and_writing_free_pages();
}
