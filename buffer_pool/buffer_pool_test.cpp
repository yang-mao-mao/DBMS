#include "BufferPoolManager.h"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

// MinGW 的数学头文件定义了同名宏，测试中取消它以使用页类型枚举。
#ifdef OVERFLOW
#undef OVERFLOW
#endif

namespace {

class TemporaryDatabase {
public:
    TemporaryDatabase() {
        static std::uint64_t sequence = 0;
        const auto timestamp = std::chrono::steady_clock::now()
                                   .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("db_test_buffer_" + std::to_string(timestamp)
               + "_" + std::to_string(sequence++) + ".db");
    }

    ~TemporaryDatabase() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    TemporaryDatabase(const TemporaryDatabase&) = delete;
    TemporaryDatabase& operator=(const TemporaryDatabase&) = delete;

    std::unique_ptr<DiskManager> open() const {
        auto disk = DiskManager::open(path_.string());
        assert(disk != nullptr);
        return disk;
    }

    Page read(page_id_t page_id) const {
        std::fstream file(path_, std::ios::binary | std::ios::in);
        Page page{};
        assert(::read_page(file, page_id, page));
        return page;
    }

    void corrupt(page_id_t page_id) const {
        Page page = read(page_id);
        page.header.checksum ^= 1u;
        std::fstream file(path_, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
        file.write(reinterpret_cast<const char*>(&page), sizeof(Page));
        file.flush();
        assert(file.good());
    }

private:
    std::filesystem::path path_;
};

std::string first_record(const Page& page) {
    std::uint16_t length = 0;
    const char* data = page.get_record(0, length);
    assert(data != nullptr);
    return std::string(data, length);
}

void set_record(Page& page, const std::string& value) {
    const auto length = static_cast<std::uint16_t>(value.size());
    if (page.slot_count() == 0) {
        assert(page.insert_record(value.data(), length) == 0);
    } else {
        assert(page.update_record(0, value.data(), length));
    }
    assert(page.validate());
}

void test_constructor_arguments() {
    TemporaryDatabase database;
    bool threw = false;
    try {
        BufferPoolManager pool(0, database.open());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
    threw = false;
    try {
        BufferPoolManager pool(1, nullptr);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

void test_frame_reset() {
    Frame frame;
    assert(frame.is_empty());
    assert(!frame.is_pinned());
    frame.page.init(9, PageType::DATA);
    frame.page_id = 9;
    frame.pin_count = 2;
    frame.is_dirty = true;
    assert(!frame.is_empty());
    assert(frame.is_pinned());
    frame.reset();
    assert(frame.is_empty());
    assert(!frame.is_pinned());
    assert(!frame.is_dirty);
}

void test_cache_hits_and_pins() {
    TemporaryDatabase database;
    auto disk = database.open();
    const auto first = disk->allocate_page();
    const auto second = disk->allocate_page();
    BufferPoolManager pool(1, std::move(disk));
    Page* page = pool.fetch_page(first);
    assert(page != nullptr);
    assert(pool.fetch_page(first) == page);
    assert(!pool.delete_page(first));
    assert(pool.unpin_page(first, false));
    assert(pool.fetch_page(second) == nullptr);
    assert(pool.unpin_page(first, false));
    assert(!pool.unpin_page(first, false));
    assert(pool.fetch_page(second) != nullptr);
    assert(!pool.unpin_page(first, false));
    assert(pool.unpin_page(second, false));
    assert(!pool.unpin_page(second, false));
}

void test_page_types_and_invalid_arguments() {
    TemporaryDatabase database;
    auto disk = database.open();
    DiskManager* observer = disk.get();
    BufferPoolManager pool(3, std::move(disk));
    for (const auto type : {PageType::DATA, PageType::INDEX, PageType::OVERFLOW}) {
        page_id_t page_id = INVALID_PAGE;
        Page* page = pool.new_page(page_id, type);
        assert(page != nullptr);
        assert(page->header.page_id == page_id);
        assert(page->header.page_type == type);
        assert(page->slot_count() == 0);
        assert(page->validate());
        assert(pool.unpin_page(page_id, false));
    }
    const auto count = observer->page_count();
    for (const auto type : {PageType::INVALID, PageType::META, PageType::FREE,
                            static_cast<PageType>(99)}) {
        page_id_t page_id = 123;
        assert(pool.new_page(page_id, type) == nullptr);
        assert(page_id == 123);
        assert(observer->page_count() == count);
    }
    assert(pool.fetch_page(INVALID_PAGE) == nullptr);
    assert(!pool.unpin_page(INVALID_PAGE, true));
    assert(!pool.flush_page(INVALID_PAGE));
    assert(!pool.flush_page(999));
}

void test_all_pinned_fetch() {
    TemporaryDatabase database;
    auto disk = database.open();
    const auto first = disk->allocate_page();
    const auto second = disk->allocate_page();
    const auto third = disk->allocate_page();
    BufferPoolManager pool(2, std::move(disk));
    Page* first_page = pool.fetch_page(first);
    Page* second_page = pool.fetch_page(second);
    assert(first_page != nullptr && second_page != nullptr);
    assert(pool.fetch_page(third) == nullptr);
    assert(first_page->header.page_id == first);
    assert(second_page->header.page_id == second);
    assert(pool.unpin_page(first, false));
    assert(pool.fetch_page(third) == first_page);
    assert(second_page->header.page_id == second);
    assert(pool.unpin_page(second, false));
    assert(pool.unpin_page(third, false));
}

void test_full_pool_new_page_noop() {
    TemporaryDatabase database;
    auto disk = database.open();
    DiskManager* observer = disk.get();
    BufferPoolManager pool(1, std::move(disk));
    page_id_t first = INVALID_PAGE;
    assert(pool.new_page(first) != nullptr);
    const auto count = observer->page_count();
    for (int attempt = 0; attempt < 8; ++attempt) {
        page_id_t output = 123;
        assert(pool.new_page(output) == nullptr);
        assert(output == 123);
        assert(observer->page_count() == count);
    }
    assert(pool.unpin_page(first, false));
    page_id_t second = INVALID_PAGE;
    assert(pool.new_page(second) != nullptr);
    assert(second == first + 1);
    assert(pool.unpin_page(second, false));
}

void test_dirty_eviction() {
    TemporaryDatabase database;
    auto disk = database.open();
    const auto first = disk->allocate_page();
    const auto second = disk->allocate_page();
    BufferPoolManager pool(1, std::move(disk));
    Page* page = pool.fetch_page(first);
    assert(page != nullptr);
    set_record(*page, "dirty eviction");
    assert(pool.unpin_page(first, true));
    assert(pool.fetch_page(second) != nullptr);
    assert(first_record(database.read(first)) == "dirty eviction");
    assert(pool.unpin_page(second, false));
    page = pool.fetch_page(first);
    assert(page != nullptr);
    assert(first_record(*page) == "dirty eviction");
    assert(pool.unpin_page(first, false));
}

void test_clean_eviction() {
    TemporaryDatabase database;
    auto disk = database.open();
    const auto first = disk->allocate_page();
    const auto second = disk->allocate_page();
    BufferPoolManager pool(1, std::move(disk));
    Page* page = pool.fetch_page(first);
    assert(page != nullptr);
    page->header.checksum ^= 1u;
    assert(pool.unpin_page(first, false));
    assert(pool.fetch_page(second) != nullptr);
    assert(database.read(first).validate());
    assert(pool.unpin_page(second, false));
}

void test_dirty_flag_is_sticky() {
    TemporaryDatabase database;
    auto disk = database.open();
    const auto first = disk->allocate_page();
    const auto second = disk->allocate_page();
    BufferPoolManager pool(1, std::move(disk));
    Page* page = pool.fetch_page(first);
    assert(page != nullptr);
    assert(pool.fetch_page(first) == page);
    set_record(*page, "keep dirty");
    assert(pool.unpin_page(first, true));
    assert(pool.unpin_page(first, false));
    assert(!pool.unpin_page(first, true));
    assert(pool.fetch_page(second) != nullptr);
    assert(first_record(database.read(first)) == "keep dirty");
    assert(pool.unpin_page(second, false));
}

void test_flush_pinned_page() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    page_id_t page_id = INVALID_PAGE;
    Page* page = pool.new_page(page_id);
    assert(page != nullptr);
    set_record(*page, "before unpin");
    assert(pool.flush_page(page_id));
    assert(first_record(database.read(page_id)) == "before unpin");
    set_record(*page, "after first flush");
    assert(pool.flush_page(page_id));
    assert(first_record(database.read(page_id)) == "after first flush");
    assert(!pool.delete_page(page_id));
    assert(pool.unpin_page(page_id, false));
    assert(!pool.unpin_page(page_id, false));
}

void test_flush_all_pages() {
    TemporaryDatabase database;
    BufferPoolManager pool(3, database.open());
    assert(pool.flush_all_pages());
    std::array<page_id_t, 3> ids{};
    for (auto& page_id : ids) {
        Page* page = pool.new_page(page_id);
        assert(page != nullptr);
        set_record(*page, "page " + std::to_string(page_id));
    }
    assert(pool.flush_all_pages());
    for (const auto page_id : ids) {
        assert(first_record(database.read(page_id)) == "page " + std::to_string(page_id));
        assert(pool.unpin_page(page_id, false));
    }
}

void test_flush_failure_preserves_dirty() {
    for (const bool already_dirty : {false, true}) {
        TemporaryDatabase database;
        auto disk = database.open();
        const auto first = disk->allocate_page();
        const auto second = disk->allocate_page();
        BufferPoolManager pool(1, std::move(disk));
        Page* page = pool.fetch_page(first);
        assert(page != nullptr);
        set_record(*page, "retry dirty write");
        page->header.checksum ^= 1u;
        if (already_dirty) assert(pool.unpin_page(first, true));
        assert(!pool.flush_page(first));
        if (!already_dirty) assert(pool.unpin_page(first, false));
        assert(database.read(first).slot_count() == 0);
        page->update_checksum();
        assert(pool.fetch_page(second) != nullptr);
        assert(first_record(database.read(first)) == "retry dirty write");
        assert(pool.unpin_page(second, false));
    }
}

void test_flush_all_continues_after_failure() {
    for (std::size_t bad_index = 0; bad_index < 3; ++bad_index) {
        TemporaryDatabase database;
        BufferPoolManager pool(3, database.open());
        std::array<page_id_t, 3> ids{};
        std::array<Page*, 3> pages{};
        for (std::size_t index = 0; index < ids.size(); ++index) {
            pages[index] = pool.new_page(ids[index]);
            assert(pages[index] != nullptr);
            set_record(*pages[index], "flush survivor");
            assert(pool.unpin_page(ids[index], true));
        }
        pages[bad_index]->header.checksum ^= 1u;
        assert(!pool.flush_all_pages());
        for (std::size_t index = 0; index < ids.size(); ++index) {
            if (index != bad_index) {
                assert(first_record(database.read(ids[index])) == "flush survivor");
            }
        }
        pages[bad_index]->update_checksum();
        assert(pool.flush_all_pages());
        assert(first_record(database.read(ids[bad_index])) == "flush survivor");
    }
}

void check_failed_fetch_preserves_cache(bool corrupt_target) {
    TemporaryDatabase database;
    auto disk = database.open();
    const auto first = disk->allocate_page();
    const auto second = disk->allocate_page();
    if (corrupt_target) database.corrupt(second);
    BufferPoolManager pool(1, std::move(disk));
    Page* page = pool.fetch_page(first);
    assert(page != nullptr);
    set_record(*page, "still cached");
    assert(pool.unpin_page(first, true));
    assert(pool.fetch_page(corrupt_target ? second : 999) == nullptr);
    assert(database.read(first).slot_count() == 0);
    assert(pool.fetch_page(first) == page);
    assert(first_record(*page) == "still cached");
    assert(pool.unpin_page(first, false));
}

void test_invalid_fetch_preserves_cache() {
    check_failed_fetch_preserves_cache(false);
}

void test_corrupt_fetch_preserves_cache() {
    check_failed_fetch_preserves_cache(true);
}

void test_failed_reads_preserve_capacity() {
    TemporaryDatabase database;
    auto disk = database.open();
    const auto corrupt = disk->allocate_page();
    const auto first = disk->allocate_page();
    const auto second = disk->allocate_page();
    database.corrupt(corrupt);
    BufferPoolManager pool(2, std::move(disk));
    for (int attempt = 0; attempt < 8; ++attempt) {
        assert(pool.fetch_page(corrupt) == nullptr);
    }
    assert(pool.fetch_page(first) != nullptr);
    assert(pool.fetch_page(second) != nullptr);
    assert(pool.unpin_page(first, false));
    assert(pool.unpin_page(second, false));
}

void test_failed_eviction_preserves_victim() {
    TemporaryDatabase database;
    auto disk = database.open();
    const auto first = disk->allocate_page();
    const auto second = disk->allocate_page();
    BufferPoolManager pool(1, std::move(disk));
    Page* page = pool.fetch_page(first);
    assert(page != nullptr);
    set_record(*page, "recover victim");
    page->header.checksum ^= 1u;
    assert(pool.unpin_page(first, true));
    assert(pool.fetch_page(second) == nullptr);
    assert(pool.fetch_page(first) == page);
    assert(first_record(*page) == "recover victim");
    page->update_checksum();
    assert(pool.unpin_page(first, false));
    assert(pool.fetch_page(second) != nullptr);
    assert(first_record(database.read(first)) == "recover victim");
    assert(pool.unpin_page(second, false));
}

void test_failed_new_page_rolls_back() {
    TemporaryDatabase database;
    auto disk = database.open();
    DiskManager* observer = disk.get();
    BufferPoolManager pool(1, std::move(disk));
    page_id_t first = INVALID_PAGE;
    Page* page = pool.new_page(first);
    assert(page != nullptr);
    set_record(*page, "allocation rollback");
    page->header.checksum ^= 1u;
    assert(pool.unpin_page(first, true));
    page_id_t output = 123;
    assert(pool.new_page(output) == nullptr);
    assert(output == 123);
    assert(observer->page_count() == 3);
    assert(database.read(first + 1).header.page_type == PageType::FREE);
    assert(pool.fetch_page(first) == page);
    page->update_checksum();
    assert(pool.unpin_page(first, false));
    assert(pool.new_page(output, PageType::INDEX) != nullptr);
    assert(output == first + 1);
    assert(observer->page_count() == 3);
    assert(first_record(database.read(first)) == "allocation rollback");
    assert(pool.unpin_page(output, false));
}

void test_delete_pinned_and_reuse() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    page_id_t first = INVALID_PAGE;
    Page* page = pool.new_page(first);
    assert(page != nullptr);
    set_record(*page, "discard on delete");
    assert(!pool.delete_page(first));
    assert(pool.unpin_page(first, true));
    assert(pool.delete_page(first));
    assert(!pool.delete_page(first));
    assert(!pool.unpin_page(first, false));
    assert(!pool.flush_page(first));
    assert(database.read(first).header.page_type == PageType::FREE);
    page_id_t reused = INVALID_PAGE;
    Page* replacement = pool.new_page(reused, PageType::INDEX);
    assert(replacement == page);
    assert(reused == first);
    assert(replacement->header.page_type == PageType::INDEX);
    assert(replacement->slot_count() == 0);
    assert(replacement->validate());
    assert(pool.unpin_page(reused, false));
    assert(!pool.unpin_page(reused, false));
}

void test_delete_uncached_and_invalid() {
    TemporaryDatabase database;
    auto disk = database.open();
    const auto first = disk->allocate_page();
    BufferPoolManager pool(1, std::move(disk));
    assert(!pool.delete_page(0));
    assert(!pool.delete_page(INVALID_PAGE));
    assert(!pool.delete_page(999));
    assert(pool.delete_page(first));
    assert(!pool.delete_page(first));
    page_id_t reused = INVALID_PAGE;
    assert(pool.new_page(reused) != nullptr);
    assert(reused == first);
    assert(pool.unpin_page(reused, false));
}

void test_delete_failure_preserves_cache() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    page_id_t page_id = INVALID_PAGE;
    Page* page = pool.new_page(page_id);
    assert(page != nullptr);
    set_record(*page, "delete retry");
    assert(pool.unpin_page(page_id, true));
    database.corrupt(page_id);
    assert(!pool.delete_page(page_id));
    assert(pool.fetch_page(page_id) == page);
    assert(first_record(*page) == "delete retry");
    assert(pool.flush_page(page_id));
    assert(pool.unpin_page(page_id, false));
    assert(pool.delete_page(page_id));
}

void test_reject_free_page() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    page_id_t page_id = INVALID_PAGE;
    assert(pool.new_page(page_id) != nullptr);
    assert(pool.unpin_page(page_id, false));
    assert(pool.delete_page(page_id));
    assert(pool.fetch_page(page_id) == nullptr);
    page_id_t reused = INVALID_PAGE;
    Page* page = pool.new_page(reused, PageType::INDEX);
    assert(page != nullptr);
    assert(reused == page_id);
    assert(page->header.page_type == PageType::INDEX);
    assert(pool.unpin_page(reused, false));
    assert(!pool.unpin_page(reused, false));
}

void test_reject_meta_page() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    assert(pool.fetch_page(0) == nullptr);
    page_id_t page_id = INVALID_PAGE;
    assert(pool.new_page(page_id) != nullptr);
    assert(pool.unpin_page(page_id, false));
}

void test_destructor_persists_pages() {
    TemporaryDatabase database;
    std::array<page_id_t, 2> ids{};
    {
        BufferPoolManager pool(2, database.open());
        for (auto& page_id : ids) {
            Page* page = pool.new_page(page_id);
            assert(page != nullptr);
            set_record(*page, "persist on destruction");
        }
        assert(pool.unpin_page(ids[0], true));
    }
    auto reopened = database.open();
    for (const auto page_id : ids) {
        Page page{};
        assert(reopened->read_page(page_id, page));
        assert(first_record(page) == "persist on destruction");
    }
}

void test_destructor_continues_after_failure() {
    TemporaryDatabase database;
    std::array<page_id_t, 3> ids{};
    {
        BufferPoolManager pool(3, database.open());
        for (std::size_t index = 0; index < ids.size(); ++index) {
            Page* page = pool.new_page(ids[index]);
            assert(page != nullptr);
            set_record(*page, "destructor survivor");
            if (index == 1) page->header.checksum ^= 1u;
            assert(pool.unpin_page(ids[index], true));
        }
    }
    auto reopened = database.open();
    for (std::size_t index = 0; index < ids.size(); ++index) {
        Page page{};
        assert(reopened->read_page(ids[index], page));
        if (index == 1) {
            assert(page.slot_count() == 0);
        } else {
            assert(first_record(page) == "destructor survivor");
        }
    }
}

struct ExpectedPage {
    std::string value;
    PageType type;
    std::uint32_t pins = 0;
    Page* pinned_page = nullptr;
};

void test_randomized_model() {
    for (const std::size_t pool_size : {1u, 3u, 7u}) {
        TemporaryDatabase database;
        std::map<page_id_t, ExpectedPage> expected;
        std::mt19937 random(static_cast<std::uint32_t>(20261028 + pool_size));
        std::size_t created_pages = 0;
        std::size_t deleted_pages = 0;
        {
            BufferPoolManager pool(pool_size, database.open());
            for (int step = 0; step < 1000; ++step) {
                std::size_t pinned_frames = 0;
                for (const auto& [page_id, model] : expected) {
                    if (model.pins > 0) ++pinned_frames;
                }
                const auto operation = random() % 8;
                if (expected.empty() || (operation == 0 && expected.size() < 40)) {
                    page_id_t page_id = INVALID_PAGE;
                    const auto type = static_cast<PageType>(2 + random() % 3);
                    Page* page = pool.new_page(page_id, type);
                    assert((page != nullptr) == (pinned_frames < pool_size));
                    if (page != nullptr) {
                        assert(!expected.contains(page_id));
                        assert(page->header.page_type == type);
                        assert(page->slot_count() == 0);
                        const auto value = "new " + std::to_string(step);
                        set_record(*page, value);
                        const bool keep_pinned = random() % 4 == 0;
                        if (!keep_pinned) assert(pool.unpin_page(page_id, true));
                        expected.emplace(page_id, ExpectedPage{
                            value, type, keep_pinned ? 1u : 0u, page});
                        ++created_pages;
                    }
                    continue;
                }
                auto selected = expected.begin();
                std::advance(selected, random() % expected.size());
                const auto page_id = selected->first;
                auto& model = selected->second;
                if (operation <= 3) {
                    Page* page = pool.fetch_page(page_id);
                    const bool available = model.pins > 0 || pinned_frames < pool_size;
                    assert((page != nullptr) == available);
                    if (page != nullptr) {
                        if (model.pins > 0) assert(page == model.pinned_page);
                        assert(page->header.page_id == page_id);
                        assert(page->header.page_type == model.type);
                        assert(first_record(*page) == model.value);
                        assert(page->validate());
                        model.pinned_page = page;
                        ++model.pins;
                        if (operation == 1) {
                            model.value = "update " + std::to_string(step);
                            set_record(*page, model.value);
                        }
                        if (operation != 2 || random() % 4 != 0) {
                            assert(pool.unpin_page(page_id, operation == 1));
                            --model.pins;
                        }
                    }
                } else if (operation == 4) {
                    assert(pool.unpin_page(page_id, true) == (model.pins > 0));
                    if (model.pins > 0) --model.pins;
                } else if (operation == 5) {
                    assert(pool.delete_page(page_id) == (model.pins == 0));
                    if (model.pins == 0) {
                        expected.erase(selected);
                        ++deleted_pages;
                    }
                } else if (operation == 6) {
                    const bool flushed = pool.flush_page(page_id);
                    if (model.pins > 0) assert(flushed);
                    if (flushed) assert(first_record(database.read(page_id)) == model.value);
                } else {
                    assert(pool.flush_all_pages());
                    for (const auto& [saved_id, saved] : expected) {
                        assert(first_record(database.read(saved_id)) == saved.value);
                    }
                }
            }
            for (const auto& [page_id, model] : expected) {
                for (std::uint32_t pin = 0; pin < model.pins; ++pin) {
                    assert(pool.unpin_page(page_id, true));
                }
                assert(!pool.unpin_page(page_id, false));
            }
            assert(pool.flush_all_pages());
        }
        assert(created_pages > pool_size);
        assert(deleted_pages > 0);
        auto reopened = database.open();
        for (page_id_t page_id = 1; page_id < reopened->page_count(); ++page_id) {
            Page page{};
            assert(reopened->read_page(page_id, page));
            assert(page.validate());
            const auto found = expected.find(page_id);
            if (found == expected.end()) {
                assert(page.header.page_type == PageType::FREE);
            } else {
                assert(page.header.page_type == found->second.type);
                assert(first_record(page) == found->second.value);
            }
        }
    }
}

using TestFunction = void (*)();

const std::pair<const char*, TestFunction> tests[] = {
    {"constructor_arguments", test_constructor_arguments},
    {"frame_reset", test_frame_reset},
    {"cache_hits_and_pins", test_cache_hits_and_pins},
    {"page_types_and_invalid_arguments", test_page_types_and_invalid_arguments},
    {"all_pinned_fetch", test_all_pinned_fetch},
    {"full_pool_new_page_noop", test_full_pool_new_page_noop},
    {"dirty_eviction", test_dirty_eviction},
    {"clean_eviction", test_clean_eviction},
    {"dirty_flag_is_sticky", test_dirty_flag_is_sticky},
    {"flush_pinned_page", test_flush_pinned_page},
    {"flush_all_pages", test_flush_all_pages},
    {"flush_failure_preserves_dirty", test_flush_failure_preserves_dirty},
    {"flush_all_continues_after_failure", test_flush_all_continues_after_failure},
    {"invalid_fetch_preserves_cache", test_invalid_fetch_preserves_cache},
    {"corrupt_fetch_preserves_cache", test_corrupt_fetch_preserves_cache},
    {"failed_reads_preserve_capacity", test_failed_reads_preserve_capacity},
    {"failed_eviction_preserves_victim", test_failed_eviction_preserves_victim},
    {"failed_new_page_rolls_back", test_failed_new_page_rolls_back},
    {"delete_pinned_and_reuse", test_delete_pinned_and_reuse},
    {"delete_uncached_and_invalid", test_delete_uncached_and_invalid},
    {"delete_failure_preserves_cache", test_delete_failure_preserves_cache},
    {"reject_free_page", test_reject_free_page},
    {"reject_meta_page", test_reject_meta_page},
    {"destructor_persists_pages", test_destructor_persists_pages},
    {"destructor_continues_after_failure", test_destructor_continues_after_failure},
    {"randomized_model", test_randomized_model},
};

} // namespace

int main(int argument_count, char* arguments[]) {
    for (const auto& [name, test] : tests) {
        if (argument_count == 1 || (argument_count == 2 && arguments[1] == std::string(name))) {
            std::cout << "Running " << name << std::endl;
            test();
            if (argument_count == 2) return 0;
        }
    }
    if (argument_count == 1) return 0;
    std::cerr << "Unknown buffer pool test or invalid arguments\n";
    return 1;
}
