#include "BPlusTree.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>

namespace {

class TemporaryDatabase {
public:
    TemporaryDatabase() {
        static std::uint64_t sequence = 0;
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("db_test_index_" + std::to_string(timestamp)
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

private:
    std::filesystem::path path_;
};

Page snapshot(BufferPoolManager& pool, page_id_t page_id) {
    Page* page = pool.fetch_page(page_id);
    assert(page != nullptr);
    const Page result = *page;
    assert(pool.unpin_page(page_id, false));
    return result;
}

std::vector<char> payload(const Page& page) {
    std::uint16_t length = 0;
    const char* bytes = page.get_record(0, length);
    assert(bytes != nullptr);
    return {bytes, bytes + length};
}

void replace_payload(Page& page, const std::vector<char>& bytes) {
    assert(page.update_record(0, bytes.data(), static_cast<std::uint16_t>(bytes.size())));
    assert(page.validate());
}

struct TreeSummary {
    std::size_t height{0};
    std::size_t entries{0};
    std::size_t internal_pages{0};
    IndexKey minimum{};
    IndexKey maximum{};
    std::vector<page_id_t> leaves;
};

TreeSummary inspect_subtree(BufferPoolManager& pool, page_id_t page_id,
                           page_id_t parent_id, std::unordered_set<page_id_t>& visited) {
    assert(visited.insert(page_id).second);
    BPlusTreePage node;
    assert(BPlusTreePage::read_from(snapshot(pool, page_id), node));
    assert(node.parent_page_id == parent_id);
    TreeSummary summary;
    if (node.is_leaf()) {
        assert(!node.entries.empty());
        summary.height = 1;
        summary.entries = node.entries.size();
        summary.minimum = node.entries.front().key;
        summary.maximum = node.entries.back().key;
        summary.leaves = {page_id};
        if (parent_id != INVALID_PAGE) {
            assert(node.entries.size() >= (BPlusTreePage::MAX_ENTRIES + 1) / 2);
        }
        return summary;
    }
    summary.internal_pages = 1;
    for (std::size_t child_index = 0; child_index < node.children.size(); ++child_index) {
        auto child = inspect_subtree(pool, node.children[child_index], page_id, visited);
        if (child_index == 0) {
            summary.height = child.height + 1;
            summary.minimum = child.minimum;
        } else {
            assert(child.height + 1 == summary.height);
            assert(summary.maximum < child.minimum);
            assert(node.keys[child_index - 1] == child.minimum);
        }
        summary.maximum = child.maximum;
        summary.entries += child.entries;
        summary.internal_pages += child.internal_pages;
        summary.leaves.insert(summary.leaves.end(), child.leaves.begin(), child.leaves.end());
    }
    if (parent_id != INVALID_PAGE) {
        assert(node.keys.size() >= BPlusTreePage::MAX_INTERNAL_KEYS / 2);
    }
    return summary;
}

TreeSummary inspect_tree(BufferPoolManager& pool, const BPlusTree& tree) {
    std::unordered_set<page_id_t> visited;
    auto summary = inspect_subtree(pool, tree.root_page_id(), INVALID_PAGE, visited);
    for (std::size_t leaf_index = 0; leaf_index < summary.leaves.size(); ++leaf_index) {
        BPlusTreePage leaf;
        assert(BPlusTreePage::read_from(snapshot(pool, summary.leaves[leaf_index]), leaf));
        const auto expected_next = leaf_index + 1 == summary.leaves.size()
            ? INVALID_PAGE : summary.leaves[leaf_index + 1];
        assert(leaf.next_page_id == expected_next);
    }
    return summary;
}

void test_internal_round_trip() {
    Page page{};
    page.init(7, PageType::INDEX);
    BPlusTreePage node;
    node.type = BPlusTreeNodeType::INTERNAL;
    node.parent_page_id = 8;
    node.children = {100};
    for (std::size_t key_index = 0; key_index < BPlusTreePage::MAX_INTERNAL_KEYS; ++key_index) {
        node.keys.push_back(static_cast<IndexKey>(key_index) - 200);
        node.children.push_back(static_cast<page_id_t>(101 + key_index));
    }
    node.keys.front() = std::numeric_limits<IndexKey>::min();
    node.keys.back() = std::numeric_limits<IndexKey>::max();
    assert(node.write_to(page));
    assert(page.validate());
    assert(payload(page).size() == BPlusTreePage::HEADER_SIZE
        + BPlusTreePage::MAX_INTERNAL_KEYS * BPlusTreePage::INTERNAL_ENTRY_SIZE);
    BPlusTreePage decoded;
    assert(BPlusTreePage::read_from(page, decoded));
    assert(decoded == node);
    const Page saved = page;
    auto reject = [&](const BPlusTreePage& invalid) {
        assert(!invalid.write_to(page));
        assert(std::memcmp(&page, &saved, sizeof(Page)) == 0);
    };
    auto invalid = node;
    invalid.children.pop_back();
    reject(invalid);
    invalid = node;
    invalid.children[1] = invalid.children[0];
    reject(invalid);
    for (const auto invalid_child : {0u, INVALID_PAGE, 7u, 8u}) {
        invalid = node;
        invalid.children[0] = invalid_child;
        reject(invalid);
    }
    invalid = node;
    invalid.keys[1] = invalid.keys[0];
    reject(invalid);
    invalid = node;
    invalid.next_page_id = 99;
    reject(invalid);
    invalid = node;
    invalid.entries.push_back({1, {9, 0}});
    reject(invalid);
    invalid = node;
    invalid.keys.clear();
    invalid.children.resize(1);
    reject(invalid);
    invalid = node;
    invalid.keys.push_back(std::numeric_limits<IndexKey>::max());
    invalid.children.push_back(900);
    reject(invalid);

    node.keys = {-9, 10};
    node.children = {100, 101, 102};
    assert(node.write_to(page));
    const auto bytes = payload(page);
    auto reject_bytes = [&](const std::vector<char>& malformed) {
        Page broken = page;
        replace_payload(broken, malformed);
        BPlusTreePage output = node;
        assert(!BPlusTreePage::read_from(broken, output));
        assert(output == node);
    };
    for (std::size_t length = 1; length < bytes.size(); ++length) {
        reject_bytes({bytes.begin(), bytes.begin() + length});
    }
    auto malformed = bytes;
    std::copy_n(bytes.begin() + 16, 4, malformed.begin() + 28);
    reject_bytes(malformed);
    malformed = bytes;
    malformed.at(16) = 7;
    reject_bytes(malformed);
}

void test_leaf_round_trip() {
    assert(!RID{}.is_valid());
    assert((!RID{0, 0}.is_valid()));
    assert((RID{8, 3}.is_valid()));
    assert((RID{8, 3} == RID{8, 3}));
    static_assert(BPlusTreePage::MAX_ENTRIES > 0);

    Page page{};
    page.init(7, PageType::INDEX);
    page.header.lsn = 123;
    page.update_checksum();
    BPlusTreePage node;
    node.parent_page_id = 12;
    node.next_page_id = 13;
    node.entries = {
        {std::numeric_limits<IndexKey>::min(), {3, 1}},
        {-42, {4, 2}}, {0, {5, 3}},
        {std::numeric_limits<IndexKey>::max(), {6, 4}}
    };
    assert(node.write_to(page));
    assert(page.validate());
    assert(page.slot_count() == 1);
    assert(page.header.page_id == 7);
    assert(page.header.lsn == 123);
    assert(payload(page).size() == BPlusTreePage::HEADER_SIZE
        + node.entries.size() * BPlusTreePage::ENTRY_SIZE);
    BPlusTreePage decoded;
    assert(BPlusTreePage::read_from(page, decoded));
    assert(decoded == node);

    node.entries.clear();
    for (std::size_t entry_index = 0; entry_index < BPlusTreePage::MAX_ENTRIES; ++entry_index) {
        node.entries.push_back({static_cast<IndexKey>(entry_index),
            {9, static_cast<std::uint16_t>(entry_index)}});
    }
    assert(node.write_to(page));
    assert(BPlusTreePage::read_from(page, decoded));
    assert(decoded == node);
    assert(BPlusTreePage::ENTRY_SIZE > page.free_bytes());
    node.entries.clear();
    assert(node.write_to(page));
    assert(BPlusTreePage::read_from(page, decoded));
    assert(decoded == node);
    assert(page.slot_count() == 1);
    assert(page.header.fragment_bytes == 0);
}

void test_leaf_rejects_invalid_data() {
    Page page{};
    page.init(7, PageType::INDEX);
    BPlusTreePage valid;
    valid.entries = {{1, {3, 0}}, {2, {4, 1}}};
    assert(valid.write_to(page));
    const Page original = page;
    auto expect_write_failure = [&](const BPlusTreePage& node) {
        assert(!node.write_to(page));
        assert(std::memcmp(&page, &original, sizeof(Page)) == 0);
    };
    BPlusTreePage invalid = valid;
    std::swap(invalid.entries[0], invalid.entries[1]);
    expect_write_failure(invalid);
    invalid = valid;
    invalid.entries[1].key = 1;
    expect_write_failure(invalid);
    invalid = valid;
    invalid.entries[0].rid = {};
    expect_write_failure(invalid);
    invalid = valid;
    invalid.parent_page_id = 0;
    expect_write_failure(invalid);
    invalid.parent_page_id = 7;
    expect_write_failure(invalid);
    invalid = valid;
    invalid.next_page_id = 7;
    expect_write_failure(invalid);
    invalid.entries.resize(BPlusTreePage::MAX_ENTRIES + 1);
    expect_write_failure(invalid);

    BPlusTreePage output = valid;
    page.header.checksum ^= 1u;
    assert(!BPlusTreePage::read_from(page, output));
    assert(!valid.write_to(page));
    assert(output == valid);
    page = original;
    assert(page.delete_record(0));
    assert(!BPlusTreePage::read_from(page, output));
    page = original;
    assert(page.insert_record("extra", 5) == 1);
    assert(!BPlusTreePage::read_from(page, output));
    assert(!valid.write_to(page));
    page.init(7, PageType::DATA);
    assert(!valid.write_to(page));
    assert(!BPlusTreePage::read_from(page, output));
    page.init(0, PageType::INDEX);
    assert(!valid.write_to(page));
    page.init(7, PageType::INDEX);
    assert(!BPlusTreePage::read_from(page, output));
    assert(output == valid);
}

void test_malformed_payloads() {
    Page original{};
    original.init(7, PageType::INDEX);
    BPlusTreePage node;
    node.entries = {{1, {9, 0}}, {2, {10, 1}}};
    assert(node.write_to(original));
    const auto bytes = payload(original);
    auto expect_read_failure = [&](const std::vector<char>& broken_bytes) {
        Page broken = original;
        replace_payload(broken, broken_bytes);
        BPlusTreePage output = node;
        assert(!BPlusTreePage::read_from(broken, output));
        assert(output == node);
    };
    for (const auto offset : {0u, 4u, 6u, 8u, 9u, 10u}) {
        auto broken_bytes = bytes;
        broken_bytes[offset] ^= 0x02;
        expect_read_failure(broken_bytes);
    }
    for (const auto offset : {12u, 16u, 28u}) {
        auto broken_bytes = bytes;
        for (std::size_t byte_index = 0; byte_index < 4; ++byte_index) {
            broken_bytes.at(offset + byte_index) = 0;
        }
        expect_read_failure(broken_bytes);
    }
    auto broken_bytes = bytes;
    for (std::size_t byte_index = 16; byte_index < 20; ++byte_index) {
        broken_bytes.at(byte_index) = 0;
    }
    broken_bytes.at(16) = 7;
    expect_read_failure(broken_bytes);
    broken_bytes = bytes;
    std::copy_n(bytes.begin() + BPlusTreePage::HEADER_SIZE, sizeof(IndexKey),
        broken_bytes.begin() + BPlusTreePage::HEADER_SIZE + BPlusTreePage::ENTRY_SIZE);
    expect_read_failure(broken_bytes);
    broken_bytes = bytes;
    broken_bytes[BPlusTreePage::HEADER_SIZE] = 3;
    expect_read_failure(broken_bytes);
    for (std::size_t length = 1; length < bytes.size(); ++length) {
        expect_read_failure(std::vector<char>(bytes.begin(), bytes.begin() + length));
    }
    broken_bytes = bytes;
    broken_bytes.push_back(0);
    expect_read_failure(broken_bytes);
}

void test_header_round_trip() {
    Page page{};
    page.init(7, PageType::INDEX);
    BPlusTreeHeaderPage header{88};
    assert(header.write_to(page));
    assert(page.validate());
    assert(payload(page).size() == BPlusTreeHeaderPage::RECORD_SIZE);
    BPlusTreeHeaderPage decoded;
    assert(BPlusTreeHeaderPage::read_from(page, decoded));
    assert(decoded.root_page_id == 88);
    BPlusTreePage leaf;
    assert(!BPlusTreePage::read_from(page, leaf));
    const Page original = page;
    for (const auto root_page_id : {0u, INVALID_PAGE, 7u}) {
        assert(!BPlusTreeHeaderPage{root_page_id}.write_to(page));
        assert(std::memcmp(&page, &original, sizeof(Page)) == 0);
        auto bytes = payload(original);
        for (std::size_t byte_index = 0; byte_index < sizeof(root_page_id); ++byte_index) {
            bytes[8 + byte_index] = static_cast<char>((root_page_id >> (byte_index * 8)) & 0xFFu);
        }
        replace_payload(page, bytes);
        assert(!BPlusTreeHeaderPage::read_from(page, decoded));
        assert(decoded.root_page_id == 88);
        page = original;
    }
    auto bytes = payload(page);
    bytes[4] = 2;
    replace_payload(page, bytes);
    assert(!BPlusTreeHeaderPage::read_from(page, decoded));
    page = original;
    assert(leaf.write_to(page));
    assert(!BPlusTreeHeaderPage::read_from(page, decoded));
}

void test_node_persistence() {
    TemporaryDatabase database;
    page_id_t node_page_id = INVALID_PAGE;
    BPlusTreePage node;
    node.entries = {{-100, {4, 0}}, {1001, {8, 3}}};
    {
        BufferPoolManager pool(1, database.open());
        Page* page = pool.new_page(node_page_id, PageType::INDEX);
        assert(page != nullptr);
        assert(node.write_to(*page));
        assert(pool.unpin_page(node_page_id, true));
        page_id_t other_page_id = INVALID_PAGE;
        assert(pool.new_page(other_page_id) != nullptr);
        assert(pool.unpin_page(other_page_id, false));
        BPlusTreePage decoded;
        assert(BPlusTreePage::read_from(snapshot(pool, node_page_id), decoded));
        assert(decoded == node);
        assert(pool.flush_all_pages());
    }
    {
        BufferPoolManager pool(1, database.open());
        const Page page = snapshot(pool, node_page_id);
        assert(page.validate());
        BPlusTreePage decoded;
        assert(BPlusTreePage::read_from(page, decoded));
        assert(decoded == node);
    }
}

void test_tree_insert_find() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    page_id_t data_page_id = INVALID_PAGE;
    Page* data_page = pool.new_page(data_page_id);
    assert(data_page != nullptr);
    const std::string record = "student record";
    const int slot_id = data_page->insert_record(record.data(), static_cast<std::uint16_t>(record.size()));
    assert(slot_id >= 0);
    const RID expected{data_page_id, static_cast<std::uint16_t>(slot_id)};
    assert(pool.unpin_page(data_page_id, true));

    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    assert(tree->header_page_id() != tree->root_page_id());
    const std::array<IndexKey, 5> keys{
        42, std::numeric_limits<IndexKey>::min(), -4,
        std::numeric_limits<IndexKey>::max(), 0
    };
    for (const auto key : keys) {
        assert(tree->insert(key, expected) == BPlusTree::InsertResult::INSERTED);
        RID found;
        assert(tree->find(key, found) == BPlusTree::FindResult::FOUND);
        assert(found == expected);
    }
    assert(tree->insert(42, {99, 5}) == BPlusTree::InsertResult::DUPLICATE_KEY);
    assert(tree->insert(100, {}) == BPlusTree::InsertResult::INVALID_RID);
    assert(tree->insert(100, {0, 0}) == BPlusTree::InsertResult::INVALID_RID);
    RID found = expected;
    assert(tree->find(100, found) == BPlusTree::FindResult::NOT_FOUND);
    assert(found == expected);
    assert(tree->find(42, found) == BPlusTree::FindResult::FOUND);
    assert(found == expected);
    const Page stored = snapshot(pool, found.page_id);
    std::uint16_t length = 0;
    const char* bytes = stored.get_record(found.slot_id, length);
    assert(bytes != nullptr);
    assert(std::string(bytes, length) == record);
    BPlusTreePage node;
    assert(BPlusTreePage::read_from(snapshot(pool, tree->root_page_id()), node));
    assert(node.entries.size() == keys.size());
    assert(std::is_sorted(node.entries.begin(), node.entries.end(),
        [](const auto& left, const auto& right) { return left.key < right.key; }));
}

void test_tree_reopen() {
    TemporaryDatabase database;
    std::array<page_id_t, 2> header_page_ids{};
    std::array<page_id_t, 2> root_page_ids{};
    const std::string record = "persisted student";
    RID expected;
    {
        BufferPoolManager pool(1, database.open());
        Page* page = pool.new_page(expected.page_id);
        assert(page != nullptr);
        const int slot_id = page->insert_record(record.data(), static_cast<std::uint16_t>(record.size()));
        assert(slot_id >= 0);
        expected.slot_id = static_cast<std::uint16_t>(slot_id);
        assert(pool.unpin_page(expected.page_id, true));
        for (std::size_t tree_index = 0; tree_index < header_page_ids.size(); ++tree_index) {
            auto tree = BPlusTree::create(pool);
            assert(tree != nullptr);
            header_page_ids[tree_index] = tree->header_page_id();
            root_page_ids[tree_index] = tree->root_page_id();
            assert(tree->insert(1001 + static_cast<IndexKey>(tree_index), expected)
                == BPlusTree::InsertResult::INSERTED);
        }
        assert(header_page_ids[0] != header_page_ids[1]);
        assert(pool.flush_all_pages());
    }
    {
        BufferPoolManager pool(1, database.open());
        for (std::size_t tree_index = 0; tree_index < header_page_ids.size(); ++tree_index) {
            auto tree = BPlusTree::open(pool, header_page_ids[tree_index]);
            assert(tree != nullptr);
            assert(tree->root_page_id() == root_page_ids[tree_index]);
            BPlusTreeHeaderPage header;
            assert(BPlusTreeHeaderPage::read_from(snapshot(pool, tree->header_page_id()), header));
            assert(header.root_page_id == tree->root_page_id());
            RID found;
            assert(tree->find(1001 + static_cast<IndexKey>(tree_index), found)
                == BPlusTree::FindResult::FOUND);
            assert(found == expected);
            assert(tree->find(1002 - static_cast<IndexKey>(tree_index), found)
                == BPlusTree::FindResult::NOT_FOUND);
            const Page stored = snapshot(pool, found.page_id);
            std::uint16_t length = 0;
            const char* bytes = stored.get_record(found.slot_id, length);
            assert(bytes != nullptr);
            assert(std::string(bytes, length) == record);
        }
    }
}

void test_tree_leaf_split() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    for (std::size_t remaining = BPlusTreePage::MAX_ENTRIES; remaining > 0; --remaining) {
        assert(tree->insert(static_cast<IndexKey>(remaining), {9, static_cast<std::uint16_t>(remaining)})
            == BPlusTree::InsertResult::INSERTED);
    }
    const Page before = snapshot(pool, tree->root_page_id());
    const auto old_root_id = tree->root_page_id();
    assert(tree->insert(99999, {9, 0}) == BPlusTree::InsertResult::INSERTED);
    assert(tree->insert(1, {9, 0}) == BPlusTree::InsertResult::DUPLICATE_KEY);
    assert(tree->root_page_id() != old_root_id);
    BPlusTreePage root;
    assert(BPlusTreePage::read_from(snapshot(pool, tree->root_page_id()), root));
    assert(!root.is_leaf());
    assert(root.children.size() == 2);
    assert(root.children.front() == old_root_id);
    BPlusTreePage left;
    BPlusTreePage right;
    assert(BPlusTreePage::read_from(snapshot(pool, root.children[0]), left));
    assert(BPlusTreePage::read_from(snapshot(pool, root.children[1]), right));
    assert(left.next_page_id == root.children[1]);
    assert(right.next_page_id == INVALID_PAGE);
    assert(root.keys.front() == right.entries.front().key);
    assert(left.entries.size() + right.entries.size() == BPlusTreePage::MAX_ENTRIES + 1);
    assert(before.validate());
    for (std::size_t entry_index = 1; entry_index <= BPlusTreePage::MAX_ENTRIES; ++entry_index) {
        RID found;
        assert(tree->find(static_cast<IndexKey>(entry_index), found) == BPlusTree::FindResult::FOUND);
        assert((found == RID{9, static_cast<std::uint16_t>(entry_index)}));
    }
    RID found;
    assert(tree->find(99999, found) == BPlusTree::FindResult::FOUND);
    assert((found == RID{9, 0}));
}

void test_tree_invalid_open() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    for (const auto page_id : {0u, INVALID_PAGE, 12345u}) {
        assert(BPlusTree::open(pool, page_id) == nullptr);
    }
    page_id_t data_page_id = INVALID_PAGE;
    assert(pool.new_page(data_page_id) != nullptr);
    assert(pool.unpin_page(data_page_id, false));
    assert(BPlusTree::open(pool, data_page_id) == nullptr);
    page_id_t empty_page_id = INVALID_PAGE;
    assert(pool.new_page(empty_page_id, PageType::INDEX) != nullptr);
    assert(pool.unpin_page(empty_page_id, false));
    assert(BPlusTree::open(pool, empty_page_id) == nullptr);
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    assert(BPlusTree::open(pool, tree->root_page_id()) == nullptr);
    page_id_t bad_header_id = INVALID_PAGE;
    Page* page = pool.new_page(bad_header_id, PageType::INDEX);
    assert(page != nullptr);
    assert(BPlusTreeHeaderPage{12345}.write_to(*page));
    assert(pool.unpin_page(bad_header_id, true));
    assert(BPlusTree::open(pool, bad_header_id) == nullptr);
    page = pool.fetch_page(bad_header_id);
    assert(page != nullptr);
    assert(BPlusTreeHeaderPage{data_page_id}.write_to(*page));
    assert(pool.unpin_page(bad_header_id, true));
    assert(BPlusTree::open(pool, bad_header_id) == nullptr);
    assert(pool.delete_page(bad_header_id));
    assert(BPlusTree::open(pool, bad_header_id) == nullptr);
    assert(BPlusTree::open(pool, tree->header_page_id()) != nullptr);
}

void test_tree_many_splits() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    constexpr std::size_t count = 3000;
    std::vector<IndexKey> keys(count);
    std::iota(keys.begin(), keys.end(), -1500);
    keys.front() = std::numeric_limits<IndexKey>::min();
    keys.back() = std::numeric_limits<IndexKey>::max();
    std::mt19937_64 generator(1729);
    std::shuffle(keys.begin(), keys.end(), generator);
    for (std::size_t key_index = 0; key_index < keys.size(); ++key_index) {
        assert(tree->insert(keys[key_index], {77, static_cast<std::uint16_t>(key_index)})
            == BPlusTree::InsertResult::INSERTED);
    }
    auto reopened = BPlusTree::open(pool, tree->header_page_id());
    assert(reopened != nullptr);
    for (std::size_t key_index = 0; key_index < keys.size(); ++key_index) {
        RID found;
        assert(reopened->find(keys[key_index], found) == BPlusTree::FindResult::FOUND);
        assert((found == RID{77, static_cast<std::uint16_t>(key_index)}));
        assert(tree->insert(keys[key_index], {9, 0}) == BPlusTree::InsertResult::DUPLICATE_KEY);
    }
    const auto summary = inspect_tree(pool, *tree);
    assert(summary.entries == count);
    assert(summary.height == 2);
    assert(summary.leaves.size() > 2);
    assert(summary.minimum == std::numeric_limits<IndexKey>::min());
    assert(summary.maximum == std::numeric_limits<IndexKey>::max());
    RID unchanged{99, 3};
    assert(tree->find(1500, unchanged) == BPlusTree::FindResult::NOT_FOUND);
    assert((unchanged == RID{99, 3}));
}

void test_tree_shared_handles() {
    TemporaryDatabase database;
    BufferPoolManager pool(8, database.open());
    auto first = BPlusTree::create(pool);
    assert(first != nullptr);
    auto second = BPlusTree::open(pool, first->header_page_id());
    assert(second != nullptr);
    const auto original_root_id = first->root_page_id();
    for (IndexKey key = 0; key < 4000; ++key) {
        auto& writer = key % 2 == 0 ? first : second;
        auto& reader = key % 2 == 0 ? second : first;
        assert(writer->insert(key, {9, static_cast<std::uint16_t>(key)})
            == BPlusTree::InsertResult::INSERTED);
        RID found;
        assert(reader->find(key, found) == BPlusTree::FindResult::FOUND);
        assert((found == RID{9, static_cast<std::uint16_t>(key)}));
        assert(first->root_page_id() == second->root_page_id());
    }
    assert(first->root_page_id() != original_root_id);
    assert(inspect_tree(pool, *second).entries == 4000);
}

void test_tree_internal_split() {
    TemporaryDatabase database;
    constexpr IndexKey count = 100000;
    page_id_t header_page_id = INVALID_PAGE;
    page_id_t root_page_id = INVALID_PAGE;
    {
        BufferPoolManager pool(1024, database.open());
        auto tree = BPlusTree::create(pool);
        assert(tree != nullptr);
        header_page_id = tree->header_page_id();
        for (IndexKey key = 0; key < count; ++key) {
            assert(tree->insert(key * 2, {static_cast<page_id_t>(key + 1), 3})
                == BPlusTree::InsertResult::INSERTED);
        }
        const auto summary = inspect_tree(pool, *tree);
        assert(summary.height == 3);
        assert(summary.entries == static_cast<std::size_t>(count));
        assert(summary.internal_pages >= 4);
        for (IndexKey key = 0; key < count; ++key) {
            RID found;
            assert(tree->find(key * 2, found) == BPlusTree::FindResult::FOUND);
            assert((found == RID{static_cast<page_id_t>(key + 1), 3}));
        }
        root_page_id = tree->root_page_id();
        assert(pool.flush_all_pages());
    }
    {
        BufferPoolManager pool(8, database.open());
        auto tree = BPlusTree::open(pool, header_page_id);
        assert(tree != nullptr);
        assert(tree->root_page_id() == root_page_id);
        for (IndexKey key = 0; key < count; key += 97) {
            RID found;
            assert(tree->find(key * 2, found) == BPlusTree::FindResult::FOUND);
            assert((found == RID{static_cast<page_id_t>(key + 1), 3}));
            assert(tree->find(key * 2 + 1, found) == BPlusTree::FindResult::NOT_FOUND);
            assert(tree->insert(key * 2, {99, 0}) == BPlusTree::InsertResult::DUPLICATE_KEY);
        }
        assert(tree->insert(-1, {99, 0}) == BPlusTree::InsertResult::INSERTED);
        assert(tree->insert(count * 2, {99, 1}) == BPlusTree::InsertResult::INSERTED);
        const auto summary = inspect_tree(pool, *tree);
        assert(summary.height == 3);
        assert(summary.entries == static_cast<std::size_t>(count + 2));
        assert(pool.flush_all_pages());
    }
}

void test_tree_descending() {
    TemporaryDatabase database;
    constexpr IndexKey count = 60000;
    BufferPoolManager pool(512, database.open());
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    for (IndexKey key = count; key > 0; --key) {
        assert(tree->insert(key, {static_cast<page_id_t>(key), 0})
            == BPlusTree::InsertResult::INSERTED);
    }
    const auto summary = inspect_tree(pool, *tree);
    assert(summary.height == 3);
    assert(summary.entries == static_cast<std::size_t>(count));
    for (IndexKey key = 1; key <= count; ++key) {
        RID found;
        assert(tree->find(key, found) == BPlusTree::FindResult::FOUND);
        assert((found == RID{static_cast<page_id_t>(key), 0}));
    }
}

void test_split_failure_rollback() {
    TemporaryDatabase database;
    auto disk = database.open();
    DiskManager* observer = disk.get();
    BufferPoolManager pool(1, std::move(disk));
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    const auto original_root_id = tree->root_page_id();
    BPlusTreePage root;
    root.type = BPlusTreeNodeType::INTERNAL;
    for (std::size_t child_index = 0; child_index <= BPlusTreePage::MAX_INTERNAL_KEYS; ++child_index) {
        page_id_t page_id = INVALID_PAGE;
        Page* page = pool.new_page(page_id, PageType::INDEX);
        assert(page != nullptr);
        BPlusTreePage leaf;
        leaf.parent_page_id = original_root_id;
        const auto entry_count = child_index == BPlusTreePage::MAX_INTERNAL_KEYS
            ? BPlusTreePage::MAX_ENTRIES : std::size_t{1};
        for (std::size_t entry_index = 0; entry_index < entry_count; ++entry_index) {
            leaf.entries.push_back({static_cast<IndexKey>(child_index * 1000 + entry_index), {9, 0}});
        }
        assert(leaf.write_to(*page));
        assert(pool.unpin_page(page_id, true));
        root.children.push_back(page_id);
        if (child_index > 0) {
            root.keys.push_back(static_cast<IndexKey>(child_index * 1000));
            Page* previous = pool.fetch_page(root.children[child_index - 1]);
            assert(previous != nullptr);
            BPlusTreePage previous_leaf;
            assert(BPlusTreePage::read_from(*previous, previous_leaf));
            previous_leaf.next_page_id = page_id;
            assert(previous_leaf.write_to(*previous));
            assert(pool.unpin_page(root.children[child_index - 1], true));
        }
    }
    Page* root_page = pool.fetch_page(original_root_id);
    assert(root_page != nullptr);
    assert(root.write_to(*root_page));
    assert(pool.unpin_page(original_root_id, true));
    const auto broken_id = root.children[200];
    const Page saved_child = snapshot(pool, broken_id);
    Page* broken = pool.fetch_page(broken_id);
    assert(broken != nullptr);
    auto bytes = payload(*broken);
    bytes[0] ^= 1;
    replace_payload(*broken, bytes);
    assert(pool.unpin_page(broken_id, true));
    const Page saved_root = snapshot(pool, original_root_id);
    const Page saved_leaf = snapshot(pool, root.children.back());
    const auto original_page_count = observer->page_count();
    const IndexKey inserted_key = static_cast<IndexKey>(BPlusTreePage::MAX_INTERNAL_KEYS * 1000
        + BPlusTreePage::MAX_ENTRIES);
    assert(tree->insert(inserted_key, {9, 1}) == BPlusTree::InsertResult::PAGE_ERROR);
    assert(tree->root_page_id() == original_root_id);
    const Page after_root = snapshot(pool, original_root_id);
    const Page after_leaf = snapshot(pool, root.children.back());
    assert(std::memcmp(&saved_root, &after_root, sizeof(Page)) == 0);
    assert(std::memcmp(&saved_leaf, &after_leaf, sizeof(Page)) == 0);
    assert(observer->page_count() == original_page_count + 2);
    assert(pool.fetch_page(original_page_count) == nullptr);
    assert(pool.fetch_page(original_page_count + 1) == nullptr);
    RID found;
    assert(tree->find(inserted_key, found) == BPlusTree::FindResult::NOT_FOUND);
    broken = pool.fetch_page(broken_id);
    assert(broken != nullptr);
    *broken = saved_child;
    assert(pool.unpin_page(broken_id, true));
    assert(tree->insert(inserted_key, {9, 1}) == BPlusTree::InsertResult::INSERTED);
    assert(observer->page_count() == original_page_count + 3);
    assert(tree->find(inserted_key, found) == BPlusTree::FindResult::FOUND);
    assert((found == RID{9, 1}));
    assert(tree->root_page_id() != original_root_id);
    assert(pool.flush_all_pages());
}

void test_tree_pinned_pool() {
    TemporaryDatabase database;
    auto disk = database.open();
    DiskManager* observer = disk.get();
    BufferPoolManager pool(1, std::move(disk));
    page_id_t blocker_id = INVALID_PAGE;
    assert(pool.new_page(blocker_id) != nullptr);
    auto page_count = observer->page_count();
    assert(BPlusTree::create(pool) == nullptr);
    assert(observer->page_count() == page_count);
    assert(pool.unpin_page(blocker_id, false));
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    assert(pool.fetch_page(blocker_id) != nullptr);
    page_count = observer->page_count();
    RID found{99, 3};
    assert(tree->find(1, found) == BPlusTree::FindResult::PAGE_ERROR);
    assert((found == RID{99, 3}));
    assert(tree->insert(1, {7, 2}) == BPlusTree::InsertResult::PAGE_ERROR);
    assert(BPlusTree::open(pool, tree->header_page_id()) == nullptr);
    assert(observer->page_count() == page_count);
    assert(pool.unpin_page(blocker_id, false));
    assert(tree->find(1, found) == BPlusTree::FindResult::NOT_FOUND);
    assert(tree->insert(1, {7, 2}) == BPlusTree::InsertResult::INSERTED);
    assert(tree->find(1, found) == BPlusTree::FindResult::FOUND);
    assert((found == RID{7, 2}));
    assert(pool.fetch_page(blocker_id) != nullptr);
    assert(pool.unpin_page(blocker_id, false));
}

void test_tree_corrupt_branch() {
    TemporaryDatabase database;
    BufferPoolManager pool(8, database.open());
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    for (IndexKey key = 0; key < 1200; ++key) {
        assert(tree->insert(key, {9, static_cast<std::uint16_t>(key)})
            == BPlusTree::InsertResult::INSERTED);
    }
    const auto root_page_id = tree->root_page_id();
    const Page saved_root = snapshot(pool, root_page_id);
    BPlusTreePage root;
    assert(BPlusTreePage::read_from(saved_root, root));
    const auto leaf_id = root.children.back();
    const Page saved_leaf = snapshot(pool, leaf_id);
    BPlusTreePage leaf;
    assert(BPlusTreePage::read_from(saved_leaf, leaf));
    Page* page = pool.fetch_page(leaf_id);
    assert(page != nullptr);
    leaf.parent_page_id = INVALID_PAGE;
    assert(leaf.write_to(*page));
    assert(pool.unpin_page(leaf_id, true));
    RID found{99, 3};
    assert(tree->find(1199, found) == BPlusTree::FindResult::PAGE_ERROR);
    assert((found == RID{99, 3}));
    assert(tree->insert(1200, {9, 0}) == BPlusTree::InsertResult::PAGE_ERROR);
    page = pool.fetch_page(leaf_id);
    assert(page != nullptr);
    *page = saved_leaf;
    assert(pool.unpin_page(leaf_id, true));

    root.children.back() = tree->header_page_id();
    page = pool.fetch_page(root_page_id);
    assert(page != nullptr);
    assert(root.write_to(*page));
    assert(pool.unpin_page(root_page_id, true));
    assert(tree->find(1199, found) == BPlusTree::FindResult::PAGE_ERROR);
    page = pool.fetch_page(root_page_id);
    assert(page != nullptr);
    *page = saved_root;
    assert(pool.unpin_page(root_page_id, true));

    assert(BPlusTreePage::read_from(saved_root, root));
    std::swap(root.children[0], root.children[1]);
    page = pool.fetch_page(root_page_id);
    assert(page != nullptr);
    assert(root.write_to(*page));
    assert(pool.unpin_page(root_page_id, true));
    assert(tree->find(0, found) == BPlusTree::FindResult::PAGE_ERROR);
    assert((found == RID{99, 3}));
    page = pool.fetch_page(root_page_id);
    assert(page != nullptr);
    *page = saved_root;
    assert(pool.unpin_page(root_page_id, true));
    assert(tree->find(1199, found) == BPlusTree::FindResult::FOUND);
    assert((found == RID{9, 1199}));
    assert(inspect_tree(pool, *tree).entries == 1200);
}

void test_tree_page_identity() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    assert(tree->insert(42, {9, 0}) == BPlusTree::InsertResult::INSERTED);
    for (const auto page_id : {tree->root_page_id(), tree->header_page_id()}) {
        const Page saved = snapshot(pool, page_id);
        Page* page = pool.fetch_page(page_id);
        assert(page != nullptr);
        page->header.page_id += 100;
        page->update_checksum();
        assert(pool.unpin_page(page_id, true));
        if (page_id == tree->root_page_id()) {
            RID found{99, 3};
            assert(tree->find(42, found) == BPlusTree::FindResult::PAGE_ERROR);
            assert((found == RID{99, 3}));
            assert(tree->insert(43, {9, 1}) == BPlusTree::InsertResult::PAGE_ERROR);
        } else {
            assert(BPlusTree::open(pool, page_id) == nullptr);
        }
        page = pool.fetch_page(page_id);
        assert(page != nullptr);
        *page = saved;
        assert(pool.unpin_page(page_id, true));
        assert(BPlusTree::open(pool, tree->header_page_id()) != nullptr);
    }
    assert(pool.flush_all_pages());
}

void test_tree_corrupt_root() {
    TemporaryDatabase database;
    page_id_t header_page_id = INVALID_PAGE;
    {
        BufferPoolManager pool(1, database.open());
        auto tree = BPlusTree::create(pool);
        assert(tree != nullptr);
        header_page_id = tree->header_page_id();
        assert(tree->insert(42, {9, 0}) == BPlusTree::InsertResult::INSERTED);
        Page* page = pool.fetch_page(tree->root_page_id());
        assert(page != nullptr);
        auto bytes = payload(*page);
        bytes[0] ^= 1;
        replace_payload(*page, bytes);
        assert(pool.unpin_page(tree->root_page_id(), true));
        RID found{99, 3};
        assert(tree->find(42, found) == BPlusTree::FindResult::PAGE_ERROR);
        assert((found == RID{99, 3}));
        assert(tree->insert(43, {9, 1}) == BPlusTree::InsertResult::PAGE_ERROR);
        assert(BPlusTree::open(pool, header_page_id) == nullptr);
        assert(pool.flush_all_pages());
    }
    {
        BufferPoolManager pool(1, database.open());
        assert(BPlusTree::open(pool, header_page_id) == nullptr);
    }
}

void test_randomized_model() {
    TemporaryDatabase database;
    page_id_t header_page_id = INVALID_PAGE;
    std::map<IndexKey, RID> model;
    std::mt19937_64 generator(0xB1057EEu);
    for (std::size_t batch = 0; batch < 4; ++batch) {
        BufferPoolManager pool(1, database.open());
        auto tree = header_page_id == INVALID_PAGE
            ? BPlusTree::create(pool) : BPlusTree::open(pool, header_page_id);
        assert(tree != nullptr);
        header_page_id = tree->header_page_id();
        for (std::size_t operation = 0; operation < 400; ++operation) {
            const IndexKey key = static_cast<IndexKey>(generator() % 800) - 400;
            if (generator() % 3 != 0) {
                const RID rid{static_cast<page_id_t>(1 + generator() % 1000),
                    static_cast<std::uint16_t>(generator() % 100)};
                BPlusTree::InsertResult expected;
                if (model.contains(key)) {
                    expected = BPlusTree::InsertResult::DUPLICATE_KEY;
                } else {
                    expected = BPlusTree::InsertResult::INSERTED;
                    model.emplace(key, rid);
                }
                assert(tree->insert(key, rid) == expected);
            } else {
                const auto position = model.find(key);
                RID found{99, 3};
                if (position == model.end()) {
                    assert(tree->find(key, found) == BPlusTree::FindResult::NOT_FOUND);
                    assert((found == RID{99, 3}));
                } else {
                    assert(tree->find(key, found) == BPlusTree::FindResult::FOUND);
                    assert(found == position->second);
                }
            }
        }
        for (const auto& [key, rid] : model) {
            RID found;
            assert(tree->find(key, found) == BPlusTree::FindResult::FOUND);
            assert(found == rid);
        }
        assert(pool.flush_all_pages());
    }
}

using TestFunction = void (*)();
const std::pair<const char*, TestFunction> tests[] = {
    {"internal_round_trip", test_internal_round_trip},
    {"leaf_round_trip", test_leaf_round_trip},
    {"leaf_rejects_invalid_data", test_leaf_rejects_invalid_data},
    {"malformed_payloads", test_malformed_payloads},
    {"header_round_trip", test_header_round_trip},
    {"node_persistence", test_node_persistence},
    {"tree_insert_find", test_tree_insert_find},
    {"tree_reopen", test_tree_reopen},
    {"tree_leaf_split", test_tree_leaf_split},
    {"tree_many_splits", test_tree_many_splits},
    {"tree_shared_handles", test_tree_shared_handles},
    {"tree_internal_split", test_tree_internal_split},
    {"tree_descending", test_tree_descending},
    {"split_failure_rollback", test_split_failure_rollback},
    {"tree_invalid_open", test_tree_invalid_open},
    {"tree_pinned_pool", test_tree_pinned_pool},
    {"tree_page_identity", test_tree_page_identity},
    {"tree_corrupt_root", test_tree_corrupt_root},
    {"tree_corrupt_branch", test_tree_corrupt_branch},
    {"randomized_model", test_randomized_model}
};

}

int main(int argument_count, char* arguments[]) {
    for (const auto& [name, test] : tests) {
        if (argument_count == 1 || (argument_count == 2 && arguments[1] == std::string(name))) {
            std::cout << "Running " << name << std::endl;
            test();
            if (argument_count == 2) {
                return 0;
            }
        }
    }
    if (argument_count == 1) {
        return 0;
    }
    std::cerr << "Unknown index test or invalid arguments\n";
    return 1;
}
