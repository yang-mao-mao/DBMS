#include "ci_pan_cun_chu.h"

#include <cassert>
#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace {

std::string read_record(const Page& page, std::uint16_t slot) {
    std::uint16_t length = 0;
    const char* bytes = page.get_record(slot, length);
    if (bytes == nullptr) return {};
    return std::string(bytes, length);
}

void test_insert_delete_compact_and_purge() {
    Page page{};
    page.init(7, PageType::DATA);
    assert(page.verify_checksum());

    assert(page.insert_record("alpha", 5) == 0);
    assert(page.insert_record("bravo", 5) == 1);
    assert(page.insert_record("charlie", 7) == 2);
    assert(page.delete_record(1));
    assert(!page.delete_record(1));
    assert(page.header.fragment_bytes == 5);

    page.compact();
    assert(page.header.slot_count == 3);
    assert(read_record(page, 0) == "alpha");
    assert(page.is_deleted(1));
    assert(read_record(page, 2) == "charlie");
    assert(page.header.fragment_bytes == 0);
    assert(page.verify_checksum());

    page.purge_deleted_slots();
    assert(page.header.slot_count == 2);
    assert(read_record(page, 0) == "alpha");
    assert(read_record(page, 1) == "charlie");
    assert(page.header.free_start == sizeof(PageHeader) + 2 * sizeof(Slot));
    assert(page.verify_checksum());
}

void test_update_space_and_fragment_accounting() {
    Page page{};
    page.init(8, PageType::DATA);
    assert(page.insert_record("0123456789", 10) == 0);
    assert(page.insert_record("abcdefghij", 10) == 1);
    assert(page.update_record(0, "12345", 5));
    assert(page.header.fragment_bytes == 5);
    assert(read_record(page, 0) == "12345");

    const std::string larger(30, 'L');
    assert(page.update_record(0, larger.data(),
                              static_cast<std::uint16_t>(larger.size())));
    assert(read_record(page, 0) == larger);
    assert(read_record(page, 1) == "abcdefghij");
    assert(page.header.fragment_bytes == 0);
    assert(page.verify_checksum());

    const std::string fill(PAGE_SIZE - sizeof(PageHeader) - 3 * sizeof(Slot) - 30 - 10,
                           'F');
    assert(page.insert_record(fill.data(),
                              static_cast<std::uint16_t>(fill.size())) == 2);
    assert(!page.update_record(0, std::string(40, 'X').data(), 40));
    assert(read_record(page, 0) == larger);
}

void test_page_aliasing_and_clear() {
    Page page{};
    page.init(9, PageType::DATA);
    assert(page.insert_record("abcdef", 6) == 0);
    assert(page.insert_record("uvwxyz", 6) == 1);

    std::uint16_t length = 0;
    const char* source = page.get_record(1, length);
    assert(length == 6);
    assert(page.update_record(0, source, length));
    assert(read_record(page, 0) == "uvwxyz");

    source = page.get_record(0, length);
    assert(page.insert_record(source, length) == 2);
    assert(read_record(page, 2) == "uvwxyz");

    page.clear();
    assert(page.header.slot_count == 0);
    assert(page.header.fragment_bytes == 0);
    assert(page.header.page_id == 9);
    assert(page.header.page_type == PageType::DATA);
    assert(page.free_bytes() == PAGE_SIZE - sizeof(PageHeader));
    assert(page.verify_checksum());
    assert(page.insert_record("reused", 6) == 0);
}

void test_page_aliasing_during_compaction() {
    Page page{};
    page.init(12, PageType::DATA);
    const std::string a(1200, 'A');
    const std::string b(1200, 'B');
    const std::string c(1200, 'C');
    assert(page.insert_record(a.data(), 1200) == 0);
    assert(page.insert_record(b.data(), 1200) == 1);
    assert(page.insert_record(c.data(), 1200) == 2);
    assert(page.delete_record(1));

    std::uint16_t length = 0;
    const char* page_source = page.get_record(2, length);
    assert(length == 1200);
    assert(page.insert_record(page_source, length) == 3);
    assert(read_record(page, 0) == a);
    assert(read_record(page, 2) == c);
    assert(read_record(page, 3) == c);
    assert(page.verify_checksum());

    Page update_page{};
    update_page.init(13, PageType::DATA);
    const std::string short_record(800, 'S');
    assert(update_page.insert_record(a.data(), 1200) == 0);
    assert(update_page.insert_record(b.data(), 1200) == 1);
    assert(update_page.insert_record(short_record.data(), 800) == 2);
    assert(update_page.delete_record(1));
    page_source = update_page.get_record(0, length);
    assert(length == 1200);
    assert(update_page.update_record(2, page_source, length));
    assert(read_record(update_page, 0) == a);
    assert(read_record(update_page, 2) == a);
    assert(update_page.verify_checksum());
}

void test_corruption_guards_and_checksum() {
    Page page{};
    page.init(10, PageType::DATA);
    assert(page.insert_record("safe", 4) == 0);

    Page corrupted = page;
    corrupted.body[0] ^= 1;
    assert(!corrupted.verify_checksum());

    corrupted = page;
    Slot invalid{static_cast<std::uint16_t>(PAGE_SIZE - 1), 8, 0};
    std::memcpy(reinterpret_cast<char*>(&corrupted) + sizeof(PageHeader),
                &invalid, sizeof(invalid));
    std::uint16_t length = 99;
    assert(corrupted.get_record(0, length) == nullptr);
    assert(length == 0);
    assert(!corrupted.update_record(0, "x", 1));
    assert(!corrupted.delete_record(0));

    corrupted = page;
    Slot unsupported_flags{0, 4, SLOT_FLAG_OVERFLOW};
    std::memcpy(reinterpret_cast<char*>(&corrupted) + sizeof(PageHeader),
                &unsupported_flags, sizeof(unsupported_flags));
    std::uint16_t unsupported_length = 0;
    assert(corrupted.get_record(0, unsupported_length) == nullptr);
    assert(!corrupted.validate());
}

void test_deterministic_operation_sequence() {
    Page page{};
    page.init(11, PageType::DATA);
    std::vector<std::string> expected;
    std::vector<bool> deleted;

    for (int i = 0; i < 120; ++i) {
        const std::string value(static_cast<std::size_t>(1 + (i * 37) % 90),
                                static_cast<char>('a' + i % 26));
        const int slot = page.insert_record(value.data(),
            static_cast<std::uint16_t>(value.size()));
        if (slot < 0) {
            page.compact();
            for (std::size_t j = 0; j < expected.size(); ++j) {
                if (deleted[j]) continue;
                std::uint16_t length = 0;
                const char* record = page.get_record(static_cast<std::uint16_t>(j), length);
                assert(record != nullptr);
                assert(std::string(record, length) == expected[j]);
            }
            page.purge_deleted_slots();
            std::size_t compacted_slot = 0;
            for (std::size_t j = 0; j < expected.size(); ++j) {
                if (deleted[j]) continue;
                assert(read_record(page, static_cast<std::uint16_t>(compacted_slot))
                       == expected[j]);
                ++compacted_slot;
            }
            assert(compacted_slot == page.slot_count());
            break;
        }
        assert(static_cast<std::size_t>(slot) == expected.size());
        expected.push_back(value);
        deleted.push_back(false);

        if (i % 9 == 4 && expected.size() > 3) {
            const std::size_t target = expected.size() / 3;
            if (!deleted[target]) {
                assert(page.delete_record(static_cast<std::uint16_t>(target)));
                deleted[target] = true;
            }
        }
        if (i % 11 == 8 && expected.size() > 2) {
            const std::size_t target = expected.size() / 2;
            if (!deleted[target]) {
                const std::string replacement(static_cast<std::size_t>(1 + (i * 13) % 120),
                                              'Z');
                if (page.update_record(static_cast<std::uint16_t>(target),
                                       replacement.data(),
                                       static_cast<std::uint16_t>(replacement.size()))) {
                    expected[target] = replacement;
                }
            }
        }
        assert(page.verify_checksum());
    }
}

void verify_against_model(const Page& page,
                          const std::vector<std::optional<std::string>>& expected) {
    assert(page.verify_checksum());
    assert(page.slot_count() == expected.size());
    assert(page.header.free_start <= page.header.free_end);
    assert(page.header.free_end <= PAGE_SIZE);

    std::size_t live_bytes = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        std::uint16_t length = 0;
        const char* record = page.get_record(static_cast<std::uint16_t>(i), length);
        if (!expected[i].has_value()) {
            assert(record == nullptr);
            assert(length == 0);
            assert(page.is_deleted(static_cast<std::uint16_t>(i)));
            continue;
        }
        assert(record != nullptr);
        assert(std::string(record, length) == *expected[i]);
        live_bytes += expected[i]->size();
    }
    assert(live_bytes + page.header.fragment_bytes
           == PAGE_SIZE - page.header.free_end);
}

void test_randomized_model() {
    Page page{};
    page.init(14, PageType::DATA);
    std::vector<std::optional<std::string>> expected;
    std::mt19937 random(0xDBDBDBDBu);

    for (int step = 0; step < 1500; ++step) {
        const int operation = static_cast<int>(random() % 10);
        if (operation <= 3) {
            const std::size_t length = 1 + random() % 180;
            const std::string value(length,
                static_cast<char>('a' + random() % 26));
            const int slot = page.insert_record(
                value.data(), static_cast<std::uint16_t>(value.size()));
            if (slot >= 0) {
                assert(static_cast<std::size_t>(slot) == expected.size());
                expected.emplace_back(value);
            }
        } else if (operation <= 6 && !expected.empty()) {
            const std::size_t slot = random() % expected.size();
            const std::size_t length = 1 + random() % 220;
            const std::string value(length,
                static_cast<char>('A' + random() % 26));
            const bool updated = page.update_record(
                static_cast<std::uint16_t>(slot), value.data(),
                static_cast<std::uint16_t>(value.size()));
            if (!expected[slot].has_value()) {
                assert(!updated);
            } else if (updated) {
                expected[slot] = value;
            }
        } else if (operation == 7 && !expected.empty()) {
            const std::size_t slot = random() % expected.size();
            const bool deleted = page.delete_record(static_cast<std::uint16_t>(slot));
            assert(deleted == expected[slot].has_value());
            expected[slot].reset();
        } else if (operation == 8) {
            page.compact();
        } else if (operation == 9) {
            page.purge_deleted_slots();
            std::vector<std::optional<std::string>> live;
            for (auto& record : expected) {
                if (record.has_value()) live.push_back(record);
            }
            expected = std::move(live);
        }
        verify_against_model(page, expected);
    }
}

} // namespace

int main() {
    test_insert_delete_compact_and_purge();
    test_update_space_and_fragment_accounting();
    test_page_aliasing_and_clear();
    test_page_aliasing_during_compaction();
    test_corruption_guards_and_checksum();
    test_deterministic_operation_sequence();
    test_randomized_model();
}
