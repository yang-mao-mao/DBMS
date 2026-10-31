#include "BPlusTree.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>

namespace {

using Model = std::map<IndexKey, RID>;
using Entry = BPlusTreeLeafEntry;

class TemporaryDatabase {
public:
    TemporaryDatabase() {
        static std::uint64_t sequence = 0;
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("db_test_index_operations_" + std::to_string(timestamp)
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
    const Page copy = *page;
    assert(pool.unpin_page(page_id, false));
    return copy;
}

BPlusTreePage read_node(BufferPoolManager& pool, page_id_t page_id) {
    BPlusTreePage node;
    assert(BPlusTreePage::read_from(snapshot(pool, page_id), node));
    return node;
}

void write_node(BufferPoolManager& pool, page_id_t page_id, const BPlusTreePage& node) {
    Page* page = pool.fetch_page(page_id);
    assert(page != nullptr);
    assert(node.write_to(*page));
    assert(pool.unpin_page(page_id, true));
}

std::vector<Entry> expected_range(const Model& model, std::optional<IndexKey> lower = {},
                                std::optional<IndexKey> upper = {},
                                std::size_t limit = std::numeric_limits<std::size_t>::max()) {
    std::vector<Entry> entries;
    for (auto position = lower ? model.lower_bound(*lower) : model.begin();
         position != model.end() && entries.size() < limit; ++position) {
        if (upper && position->first >= *upper) {
            break;
        }
        entries.push_back({position->first, position->second});
    }
    return entries;
}

struct TreeSummary {
    std::size_t height{0};
    std::size_t count{0};
    IndexKey minimum{};
    IndexKey maximum{};
    std::vector<page_id_t> leaves;
};

TreeSummary inspect_subtree(BufferPoolManager& pool, page_id_t page_id, page_id_t parent_id,
                           std::unordered_set<page_id_t>& visited) {
    assert(visited.insert(page_id).second);
    const auto node = read_node(pool, page_id);
    assert(node.parent_page_id == parent_id);
    TreeSummary summary;
    if (node.is_leaf()) {
        if (parent_id != INVALID_PAGE) {
            assert(node.entries.size() >= BPlusTreePage::MIN_LEAF_ENTRIES);
        }
        summary.height = 1;
        summary.count = node.entries.size();
        summary.leaves.push_back(page_id);
        if (!node.entries.empty()) {
            summary.minimum = node.entries.front().key;
            summary.maximum = node.entries.back().key;
        }
        return summary;
    }
    if (parent_id != INVALID_PAGE) {
        assert(node.keys.size() >= BPlusTreePage::MIN_INTERNAL_KEYS);
    }
    for (std::size_t child_index = 0; child_index < node.children.size(); ++child_index) {
        const auto child = inspect_subtree(pool, node.children[child_index], page_id, visited);
        assert(child.count != 0);
        if (child_index == 0) {
            summary.height = child.height + 1;
            summary.minimum = child.minimum;
        } else {
            assert(summary.height == child.height + 1);
            assert(summary.maximum < child.minimum);
            assert(node.keys[child_index - 1] == child.minimum);
        }
        summary.maximum = child.maximum;
        summary.count += child.count;
        summary.leaves.insert(summary.leaves.end(), child.leaves.begin(), child.leaves.end());
    }
    return summary;
}

TreeSummary check_tree(BufferPoolManager& pool, const BPlusTree& tree, const Model& model) {
    std::unordered_set<page_id_t> visited;
    auto summary = inspect_subtree(pool, tree.root_page_id(), INVALID_PAGE, visited);
    assert(summary.count == model.size());
    for (std::size_t leaf_index = 0; leaf_index < summary.leaves.size(); ++leaf_index) {
        const auto leaf = read_node(pool, summary.leaves[leaf_index]);
        const auto expected_next = leaf_index + 1 == summary.leaves.size()
            ? INVALID_PAGE : summary.leaves[leaf_index + 1];
        assert(leaf.next_page_id == expected_next);
    }
    std::vector<Entry> entries;
    assert(tree.scan({}, {}, entries) == BPlusTree::ScanResult::SUCCESS);
    assert(entries == expected_range(model));
    return summary;
}

void populate(BPlusTree& tree, Model& model, IndexKey begin, IndexKey end) {
    for (IndexKey key = begin; key < end; ++key) {
        const RID rid{static_cast<page_id_t>(key - begin + 1), 3};
        assert(tree.insert(key, rid) == BPlusTree::InsertResult::INSERTED);
        model.emplace(key, rid);
    }
}

void test_scan_boundaries() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    std::vector<Entry> entries{{99, {9, 0}}};
    assert(tree->scan({}, {}, entries) == BPlusTree::ScanResult::SUCCESS);
    assert(entries.empty());
    Model model;
    for (const auto key : {std::numeric_limits<IndexKey>::min(), IndexKey{-10}, IndexKey{0},
                          IndexKey{10}, std::numeric_limits<IndexKey>::max()}) {
        const RID rid{9, static_cast<std::uint16_t>(model.size())};
        assert(tree->insert(key, rid) == BPlusTree::InsertResult::INSERTED);
        model.emplace(key, rid);
    }
    assert(tree->scan({}, {}, entries) == BPlusTree::ScanResult::SUCCESS);
    assert(entries == expected_range(model));
    assert(tree->scan(-10, 10, entries) == BPlusTree::ScanResult::SUCCESS);
    assert(entries == expected_range(model, -10, 10));
    assert(tree->scan({}, std::numeric_limits<IndexKey>::max(), entries) == BPlusTree::ScanResult::SUCCESS);
    assert(entries.size() == 4);
    assert(tree->scan(std::numeric_limits<IndexKey>::max(), {}, entries) == BPlusTree::ScanResult::SUCCESS);
    assert(entries == expected_range(model, std::numeric_limits<IndexKey>::max()));
    assert(tree->scan(1, 9, entries) == BPlusTree::ScanResult::SUCCESS);
    assert(entries.empty());
    assert(tree->scan(10, 10, entries) == BPlusTree::ScanResult::SUCCESS);
    assert(entries.empty());
    entries = {{99, {9, 0}}};
    const auto saved = entries;
    assert(tree->scan(11, 10, entries) == BPlusTree::ScanResult::INVALID_RANGE);
    assert(entries == saved);
    assert(tree->scan({}, {}, entries, 2) == BPlusTree::ScanResult::SUCCESS);
    assert(entries == expected_range(model, {}, {}, 2));
    assert(tree->scan({}, {}, entries, 0) == BPlusTree::ScanResult::SUCCESS);
    assert(entries.empty());
}

void test_scan_multiple_pages() {
    TemporaryDatabase database;
    Model model;
    page_id_t header_id = INVALID_PAGE;
    {
        BufferPoolManager pool(1, database.open());
        auto tree = BPlusTree::create(pool);
        assert(tree != nullptr);
        header_id = tree->header_page_id();
        populate(*tree, model, -1000, 2000);
        for (const auto lower : {-1500, -1000, -856, -1, 0, 144, 1999, 2000}) {
            for (const auto width : {0, 1, 143, 144, 500, 4000}) {
                std::vector<Entry> entries;
                assert(tree->scan(lower, lower + width, entries) == BPlusTree::ScanResult::SUCCESS);
                assert(entries == expected_range(model, lower, lower + width));
            }
        }
        std::vector<Entry> entries;
        assert(tree->scan(-800, 1800, entries, 400) == BPlusTree::ScanResult::SUCCESS);
        assert(entries == expected_range(model, -800, 1800, 400));
        check_tree(pool, *tree, model);
        assert(pool.flush_all_pages());
    }
    {
        BufferPoolManager pool(1, database.open());
        auto tree = BPlusTree::open(pool, header_id);
        assert(tree != nullptr);
        check_tree(pool, *tree, model);
    }
}

void test_scan_failure_preserves_output() {
    TemporaryDatabase database;
    BufferPoolManager pool(8, database.open());
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    Model model;
    populate(*tree, model, 0, 600);
    const auto root = read_node(pool, tree->root_page_id());
    const auto first_id = root.children.front();
    const auto last_id = root.children.back();
    const auto saved_first = read_node(pool, first_id);
    const auto saved_last = read_node(pool, last_id);
    auto broken = saved_first;
    broken.next_page_id = tree->root_page_id();
    write_node(pool, first_id, broken);
    std::vector<Entry> entries{{99, {9, 0}}};
    const auto sentinel = entries;
    assert(tree->scan({}, {}, entries) == BPlusTree::ScanResult::PAGE_ERROR);
    assert(entries == sentinel);
    assert(tree->scan({}, {}, entries, 1) == BPlusTree::ScanResult::SUCCESS);
    assert(entries == expected_range(model, {}, {}, 1));
    write_node(pool, first_id, saved_first);
    broken = saved_last;
    broken.next_page_id = first_id;
    write_node(pool, last_id, broken);
    entries = sentinel;
    assert(tree->scan({}, {}, entries) == BPlusTree::ScanResult::PAGE_ERROR);
    assert(entries == sentinel);
    write_node(pool, last_id, saved_last);
    check_tree(pool, *tree, model);
}

void test_erase_basics() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    page_id_t data_page_id = INVALID_PAGE;
    Page* data_page = pool.new_page(data_page_id);
    assert(data_page != nullptr);
    assert(data_page->insert_record("student", 7) == 0);
    assert(pool.unpin_page(data_page_id, true));
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    assert(tree->erase(1001) == BPlusTree::EraseResult::NOT_FOUND);
    assert(tree->insert(1001, {data_page_id, 0}) == BPlusTree::InsertResult::INSERTED);
    assert(tree->erase(1001) == BPlusTree::EraseResult::REMOVED);
    assert(tree->erase(1001) == BPlusTree::EraseResult::NOT_FOUND);
    const Page stored = snapshot(pool, data_page_id);
    std::uint16_t length = 0;
    const char* record = stored.get_record(0, length);
    assert(record != nullptr && std::string(record, length) == "student");
    check_tree(pool, *tree, {});
    assert(tree->insert(1001, {data_page_id, 0}) == BPlusTree::InsertResult::INSERTED);
    RID found;
    assert(tree->find(1001, found) == BPlusTree::FindResult::FOUND);
    assert((found == RID{data_page_id, 0}));
}

void test_leaf_erase_rebalance() {
    for (const auto operation : {0, 1, 2, 3, 4}) {
        TemporaryDatabase database;
        BufferPoolManager pool(1, database.open());
        auto tree = BPlusTree::create(pool);
        assert(tree != nullptr);
        Model model;
        populate(*tree, model, 0, 288);
        const auto old_root_id = tree->root_page_id();
        if (operation == 0 || operation == 4) {
            populate(*tree, model, 288, 298);
        } else if (operation == 1) {
            populate(*tree, model, -10, 0);
        }
        const IndexKey removed = operation == 0 || operation == 2 ? 0
            : operation == 4 ? 144 : 287;
        assert(tree->erase(removed) == BPlusTree::EraseResult::REMOVED);
        model.erase(removed);
        const auto summary = check_tree(pool, *tree, model);
        if (operation == 2 || operation == 3) {
            assert(summary.height == 1);
            assert(tree->root_page_id() != old_root_id);
            assert(pool.fetch_page(old_root_id) == nullptr);
        } else {
            assert(summary.height == 2);
            assert(tree->root_page_id() == old_root_id);
        }
        RID unchanged{99, 3};
        assert(tree->find(removed, unchanged) == BPlusTree::FindResult::NOT_FOUND);
        assert((unchanged == RID{99, 3}));
    }
}

void test_erase_reclaim_failure() {
    TemporaryDatabase database;
    BufferPoolManager pool(4, database.open());
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    Model model;
    populate(*tree, model, 0, 288);
    const auto retired_root_id = tree->root_page_id();
    assert(pool.fetch_page(retired_root_id) != nullptr);
    assert(tree->erase(0) == BPlusTree::EraseResult::RECLAIM_FAILED);
    model.erase(0);
    assert(tree->root_page_id() != retired_root_id);
    assert(pool.unpin_page(retired_root_id, false));
    assert(pool.delete_page(retired_root_id));
    check_tree(pool, *tree, model);
    assert(tree->erase(0) == BPlusTree::EraseResult::NOT_FOUND);
}

void test_erase_failure_preserves_tree() {
    TemporaryDatabase database;
    BufferPoolManager pool(4, database.open());
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    Model model;
    populate(*tree, model, 0, 288);
    const auto root_id = tree->root_page_id();
    const auto root = read_node(pool, root_id);
    const Page saved_root = snapshot(pool, root_id);
    const Page saved_left = snapshot(pool, root.children.front());
    const Page saved_right = snapshot(pool, root.children.back());
    Page* page = pool.fetch_page(root.children.front());
    assert(page != nullptr);
    std::uint16_t length = 0;
    const char* record = page->get_record(0, length);
    assert(record != nullptr);
    std::vector<char> bytes(record, record + length);
    bytes[0] ^= 1;
    assert(page->update_record(0, bytes.data(), static_cast<std::uint16_t>(bytes.size())));
    assert(pool.unpin_page(root.children.front(), true));
    assert(tree->erase(287) == BPlusTree::EraseResult::PAGE_ERROR);
    const Page after_root = snapshot(pool, root_id);
    const Page after_right = snapshot(pool, root.children.back());
    assert(std::memcmp(&saved_root, &after_root, sizeof(Page)) == 0);
    assert(std::memcmp(&saved_right, &after_right, sizeof(Page)) == 0);
    page = pool.fetch_page(root.children.front());
    assert(page != nullptr);
    *page = saved_left;
    assert(pool.unpin_page(root.children.front(), true));
    check_tree(pool, *tree, model);
    assert(tree->erase(287) == BPlusTree::EraseResult::REMOVED);
    model.erase(287);
    check_tree(pool, *tree, model);
}

void test_erase_all_reuse() {
    TemporaryDatabase database;
    auto disk = database.open();
    DiskManager* observer = disk.get();
    BufferPoolManager pool(1, std::move(disk));
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    Model model;
    populate(*tree, model, 0, 2500);
    const auto page_count = observer->page_count();
    std::vector<IndexKey> keys(2500);
    std::iota(keys.begin(), keys.end(), 0);
    std::mt19937_64 generator(1807);
    std::shuffle(keys.begin(), keys.end(), generator);
    for (std::size_t key_index = 0; key_index < keys.size(); ++key_index) {
        assert(tree->erase(keys[key_index]) == BPlusTree::EraseResult::REMOVED);
        model.erase(keys[key_index]);
        if (key_index % 73 == 0) {
            check_tree(pool, *tree, model);
        }
    }
    assert(check_tree(pool, *tree, model).height == 1);
    std::size_t live_index_pages = 0;
    for (page_id_t page_id = 1; page_id < observer->page_count(); ++page_id) {
        Page page;
        assert(observer->read_page(page_id, page));
        if (page.header.page_type != PageType::FREE) {
            ++live_index_pages;
        }
    }
    assert(live_index_pages == 2);
    populate(*tree, model, 0, 2500);
    assert(observer->page_count() == page_count);
    check_tree(pool, *tree, model);
}

struct ThreeLevelFixture {
    std::unique_ptr<BPlusTree> tree;
    Model model;
    page_id_t left_id{INVALID_PAGE};
    page_id_t right_id{INVALID_PAGE};
    IndexKey removed_key{};
};

ThreeLevelFixture make_three_level_fixture(BufferPoolManager& pool, bool left_spare,
                                           bool right_spare, bool target_right) {
    ThreeLevelFixture fixture;
    fixture.tree = BPlusTree::create(pool);
    assert(fixture.tree != nullptr);
    const auto root_id = fixture.tree->root_page_id();
    for (auto* page_id : {&fixture.left_id, &fixture.right_id}) {
        Page* page = pool.new_page(*page_id, PageType::INDEX);
        assert(page != nullptr);
        assert(pool.unpin_page(*page_id, false));
    }
    const auto left_count = BPlusTreePage::MIN_INTERNAL_KEYS + 1 + (left_spare ? 1 : 0);
    const auto right_count = BPlusTreePage::MIN_INTERNAL_KEYS + 1 + (right_spare ? 1 : 0);
    std::vector<page_id_t> leaves(left_count + right_count, INVALID_PAGE);
    for (auto& page_id : leaves) {
        assert(pool.new_page(page_id, PageType::INDEX) != nullptr);
        assert(pool.unpin_page(page_id, false));
    }
    BPlusTreePage left;
    BPlusTreePage right;
    left.type = right.type = BPlusTreeNodeType::INTERNAL;
    left.parent_page_id = right.parent_page_id = root_id;
    for (std::size_t leaf_index = 0; leaf_index < leaves.size(); ++leaf_index) {
        BPlusTreePage leaf;
        leaf.parent_page_id = leaf_index < left_count ? fixture.left_id : fixture.right_id;
        leaf.next_page_id = leaf_index + 1 == leaves.size() ? INVALID_PAGE : leaves[leaf_index + 1];
        for (std::size_t entry_index = 0; entry_index < BPlusTreePage::MIN_LEAF_ENTRIES; ++entry_index) {
            const auto key = static_cast<IndexKey>(leaf_index * 1000 + entry_index);
            const RID rid{9, static_cast<std::uint16_t>(entry_index)};
            leaf.entries.push_back({key, rid});
            fixture.model.emplace(key, rid);
        }
        auto& parent = leaf_index < left_count ? left : right;
        if (!parent.children.empty()) {
            parent.keys.push_back(leaf.entries.front().key);
        }
        parent.children.push_back(leaves[leaf_index]);
        write_node(pool, leaves[leaf_index], leaf);
    }
    write_node(pool, fixture.left_id, left);
    write_node(pool, fixture.right_id, right);
    BPlusTreePage root;
    root.type = BPlusTreeNodeType::INTERNAL;
    root.keys = {static_cast<IndexKey>(left_count * 1000)};
    root.children = {fixture.left_id, fixture.right_id};
    write_node(pool, root_id, root);
    fixture.removed_key = target_right ? root.keys.front() : 0;
    return fixture;
}

void test_internal_erase_rebalance() {
    for (const int scenario : {0, 1, 2, 3}) {
        std::cout << "Internal erase scenario " << scenario << std::endl;
        TemporaryDatabase database;
        BufferPoolManager pool(scenario == 2 ? 1 : 512, database.open());
        const bool left_spare = scenario == 0;
        const bool right_spare = scenario == 1;
        const bool target_right = scenario == 0 || scenario == 2;
        auto fixture = make_three_level_fixture(pool, left_spare, right_spare, target_right);
        assert(check_tree(pool, *fixture.tree, fixture.model).height == 3);
        const auto old_root_id = fixture.tree->root_page_id();
        assert(fixture.tree->erase(fixture.removed_key) == BPlusTree::EraseResult::REMOVED);
        fixture.model.erase(fixture.removed_key);
        const auto summary = check_tree(pool, *fixture.tree, fixture.model);
        if (scenario < 2) {
            assert(summary.height == 3);
            assert(fixture.tree->root_page_id() == old_root_id);
            assert(read_node(pool, fixture.left_id).keys.size() == BPlusTreePage::MIN_INTERNAL_KEYS);
            assert(read_node(pool, fixture.right_id).keys.size() == BPlusTreePage::MIN_INTERNAL_KEYS);
        } else {
            assert(summary.height == 2);
            assert(fixture.tree->root_page_id() == fixture.left_id);
            assert(pool.fetch_page(old_root_id) == nullptr);
            assert(pool.fetch_page(fixture.right_id) == nullptr);
        }
        RID found;
        assert(fixture.tree->find(fixture.removed_key, found) == BPlusTree::FindResult::NOT_FOUND);
        auto reopened = BPlusTree::open(pool, fixture.tree->header_page_id());
        assert(reopened != nullptr);
        check_tree(pool, *reopened, fixture.model);
        assert(pool.flush_all_pages());
    }
}

void test_mixed_randomized_model() {
    TemporaryDatabase database;
    Model model;
    page_id_t header_id = INVALID_PAGE;
    std::mt19937_64 generator(0xE2A5E);
    for (std::size_t batch = 0; batch < 4; ++batch) {
        BufferPoolManager pool(64, database.open());
        auto tree = header_id == INVALID_PAGE
            ? BPlusTree::create(pool) : BPlusTree::open(pool, header_id);
        assert(tree != nullptr);
        header_id = tree->header_page_id();
        if (batch == 0) {
            populate(*tree, model, -1500, 1500);
        }
        auto second = BPlusTree::open(pool, header_id);
        assert(second != nullptr);
        for (std::size_t operation = 0; operation < 1500; ++operation) {
            auto& active = operation % 2 == 0 ? tree : second;
            const IndexKey key = static_cast<IndexKey>(generator() % 6000) - 3000;
            const auto action = generator() % 10;
            if (action < 4) {
                const RID rid{static_cast<page_id_t>(generator() % 1000 + 1),
                    static_cast<std::uint16_t>(generator() % 100)};
                const auto expected = model.contains(key) ? BPlusTree::InsertResult::DUPLICATE_KEY
                    : BPlusTree::InsertResult::INSERTED;
                assert(active->insert(key, rid) == expected);
                model.emplace(key, rid);
            } else if (action < 8) {
                const auto expected = model.contains(key) ? BPlusTree::EraseResult::REMOVED
                    : BPlusTree::EraseResult::NOT_FOUND;
                assert(active->erase(key) == expected);
                model.erase(key);
            } else {
                RID found{99, 3};
                const auto position = model.find(key);
                if (position == model.end()) {
                    assert(active->find(key, found) == BPlusTree::FindResult::NOT_FOUND);
                    assert((found == RID{99, 3}));
                } else {
                    assert(active->find(key, found) == BPlusTree::FindResult::FOUND);
                    assert(found == position->second);
                }
            }
            if (operation % 37 == 0) {
                std::vector<Entry> entries;
                const auto limit = static_cast<std::size_t>(generator() % 300);
                assert(active->scan(key, key + 700, entries, limit) == BPlusTree::ScanResult::SUCCESS);
                assert(entries == expected_range(model, key, key + 700, limit));
            }
            if (operation % 113 == 0) {
                check_tree(pool, *active, model);
            }
        }
        check_tree(pool, *tree, model);
        assert(pool.flush_all_pages());
    }
}

void test_erase_cascade_reopen() {
    TemporaryDatabase database;
    Model model;
    std::vector<IndexKey> keys;
    page_id_t header_id = INVALID_PAGE;
    {
        BufferPoolManager pool(512, database.open());
        auto fixture = make_three_level_fixture(pool, true, true, false);
        assert(check_tree(pool, *fixture.tree, fixture.model).height == 3);
        header_id = fixture.tree->header_page_id();
        model = std::move(fixture.model);
        for (const auto& [key, rid] : model) {
            keys.push_back(key);
            assert(rid.is_valid());
        }
        std::mt19937_64 generator(67213);
        std::shuffle(keys.begin(), keys.end(), generator);
        assert(pool.flush_all_pages());
    }
    for (std::size_t batch = 0; batch < 4; ++batch) {
        BufferPoolManager pool(512, database.open());
        auto tree = BPlusTree::open(pool, header_id);
        assert(tree != nullptr);
        const auto first = keys.size() * batch / 4;
        const auto last = keys.size() * (batch + 1) / 4;
        for (std::size_t key_index = first; key_index < last; ++key_index) {
            assert(tree->erase(keys[key_index]) == BPlusTree::EraseResult::REMOVED);
            model.erase(keys[key_index]);
            if (key_index % 2048 == 0) {
                check_tree(pool, *tree, model);
            }
        }
        check_tree(pool, *tree, model);
        assert(pool.flush_all_pages());
    }
    assert(model.empty());
    {
        BufferPoolManager pool(1, database.open());
        auto tree = BPlusTree::open(pool, header_id);
        assert(tree != nullptr);
        assert(check_tree(pool, *tree, model).height == 1);
        populate(*tree, model, -1000, 1000);
        assert(check_tree(pool, *tree, model).height == 2);
        assert(pool.flush_all_pages());
    }
}

void test_operations_pinned_pool() {
    TemporaryDatabase database;
    BufferPoolManager pool(1, database.open());
    auto tree = BPlusTree::create(pool);
    assert(tree != nullptr);
    Model model;
    populate(*tree, model, 0, 10);
    page_id_t blocker_id = INVALID_PAGE;
    assert(pool.new_page(blocker_id) != nullptr);
    assert(tree->erase(0) == BPlusTree::EraseResult::PAGE_ERROR);
    std::vector<Entry> entries{{99, {9, 0}}};
    const auto saved = entries;
    assert(tree->scan({}, {}, entries) == BPlusTree::ScanResult::PAGE_ERROR);
    assert(entries == saved);
    assert(pool.unpin_page(blocker_id, false));
    check_tree(pool, *tree, model);
    assert(tree->erase(0) == BPlusTree::EraseResult::REMOVED);
    model.erase(0);
    check_tree(pool, *tree, model);
}

using TestFunction = void (*)();
const std::pair<const char*, TestFunction> tests[] = {
    {"scan_boundaries", test_scan_boundaries},
    {"scan_multiple_pages", test_scan_multiple_pages},
    {"scan_failure_preserves_output", test_scan_failure_preserves_output},
    {"erase_basics", test_erase_basics},
    {"leaf_erase_rebalance", test_leaf_erase_rebalance},
    {"erase_reclaim_failure", test_erase_reclaim_failure},
    {"erase_failure_preserves_tree", test_erase_failure_preserves_tree},
    {"erase_all_reuse", test_erase_all_reuse},
    {"internal_erase_rebalance", test_internal_erase_rebalance},
    {"erase_cascade_reopen", test_erase_cascade_reopen},
    {"operations_pinned_pool", test_operations_pinned_pool},
    {"mixed_randomized_model", test_mixed_randomized_model}
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
    std::cerr << "Unknown index operation test or invalid arguments\n";
    return 1;
}
