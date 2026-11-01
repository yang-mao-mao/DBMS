#include "BPlusTree.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <unordered_set>
#include <utility>

namespace {

class PagePin {
public:
    PagePin(BufferPoolManager& buffer_pool, page_id_t page_id, Page* page)
        : buffer_pool_(buffer_pool), page_(page), page_id_(page_id) {}

    ~PagePin() {
        if (page_ != nullptr) {
            (void)buffer_pool_.unpin_page(page_id_, dirty_);
        }
    }

    PagePin(const PagePin&) = delete;
    PagePin& operator=(const PagePin&) = delete;

    void mark_dirty() { dirty_ = true; }

private:
    BufferPoolManager& buffer_pool_;
    Page* page_;
    page_id_t page_id_;
    bool dirty_{false};
};

//以拷贝的方式从缓冲池中读出一个页
bool read_page_copy(BufferPoolManager& pool, page_id_t page_id, Page& copy) {
    Page* page = pool.fetch_page(page_id);
    PagePin pin(pool, page_id, page);
    if (page == nullptr || page->header.page_id != page_id
        || page->header.page_type != PageType::INDEX || !page->validate()) {
        return false;
    }
    copy = *page;
    return true;
}

//读取头页，调用read_page_copy()
bool read_header(BufferPoolManager& pool, page_id_t page_id, BPlusTreeHeaderPage& header) {
    Page copy;
    return read_page_copy(pool, page_id, copy) && BPlusTreeHeaderPage::read_from(copy, header);
}

class PageChanges {
public:
    PageChanges(BufferPoolManager& pool, page_id_t header_page_id)
        : pool_(pool), header_page_id_(header_page_id) {}

    ~PageChanges() {
        for (auto position = new_pages_.rbegin(); position != new_pages_.rend(); ++position) {
            (void)pool_.delete_page(*position);
        }
    }

    PageChanges(const PageChanges&) = delete;
    PageChanges& operator=(const PageChanges&) = delete;

    bool allocate(page_id_t& page_id) {
        new_pages_.reserve(new_pages_.size() + 1);
        Page* page = pool_.new_page(page_id, PageType::INDEX);
        PagePin pin(pool_, page_id, page);
        if (page == nullptr) {
            return false;
        }
        new_pages_.push_back(page_id);
        changes_.emplace(page_id, Change{*page, *page, true});
        return true;
    }

    bool read_node(page_id_t page_id, BPlusTreePage& node) {
        if (std::find(retired_pages_.begin(), retired_pages_.end(), page_id) != retired_pages_.end()) {
            return false;
        }
        const auto position = changes_.find(page_id);
        if (position != changes_.end()) {
            return BPlusTreePage::read_from(position->second.updated, node);
        }
        Page copy;
        return read_page_copy(pool_, page_id, copy) && BPlusTreePage::read_from(copy, node);
    }

    template <typename Node>
    bool stage(page_id_t page_id, const Node& node) {
        if (std::find(retired_pages_.begin(), retired_pages_.end(), page_id) != retired_pages_.end()) {
            return false;
        }
        auto position = changes_.find(page_id);
        if (position == changes_.end()) {
            Page copy;
            if (!read_page_copy(pool_, page_id, copy)) {
                return false;
            }
            position = changes_.emplace(page_id, Change{copy, copy, false}).first;
        }
        return node.write_to(position->second.updated);
    }

    bool reparent(page_id_t page_id, page_id_t old_parent, page_id_t new_parent) {
        BPlusTreePage node;
        if (page_id == header_page_id_ || !read_node(page_id, node)
            || node.parent_page_id != old_parent) {
            return false;
        }
        node.parent_page_id = new_parent;
        return stage(page_id, node);
    }

    void retire(page_id_t page_id) {
        retired_pages_.push_back(page_id);
        changes_.erase(page_id);
    }

    bool reclaim() {
        bool success = true;
        for (const auto page_id : retired_pages_) {
            if (!pool_.delete_page(page_id)) {
                success = false;
            }
        }
        return success;
    }

    bool commit() {
        std::vector<page_id_t> order;
        order.reserve(changes_.size());
        for (const bool new_pages : {true, false}) {
            for (const auto& [page_id, change] : changes_) {
                if (change.is_new == new_pages && page_id != header_page_id_) {
                    order.push_back(page_id);
                }
            }
        }
        if (changes_.contains(header_page_id_)) {
            order.push_back(header_page_id_);
        }
        for (const auto page_id : order) {
            Page copy;
            if (!read_page_copy(pool_, page_id, copy)
                || std::memcmp(&copy, &changes_.at(page_id).original, sizeof(Page)) != 0) {
                return false;
            }
        }

        std::size_t applied = 0;
        for (const auto page_id : order) {
            if (!replace(page_id, changes_.at(page_id).updated)) {
                bool restored = true;
                while (applied > 0) {
                    const auto previous_id = order[--applied];
                    if (!replace(previous_id, changes_.at(previous_id).original)) {
                        restored = false;
                    }
                }
                if (!restored) {
                    new_pages_.clear();
                }
                return false;
            }
            ++applied;
        }
        new_pages_.clear();
        return true;
    }

private:
    struct Change {
        Page original;
        Page updated;
        bool is_new;
    };

    bool replace(page_id_t page_id, const Page& value) {
        Page* page = pool_.fetch_page(page_id);
        PagePin pin(pool_, page_id, page);
        if (page == nullptr || page->header.page_id != page_id) {
            return false;
        }
        *page = value;
        pin.mark_dirty();
        return true;
    }

    BufferPoolManager& pool_;
    page_id_t header_page_id_;
    std::map<page_id_t, Change> changes_;
    std::vector<page_id_t> new_pages_;
    std::vector<page_id_t> retired_pages_;
};

struct PathEntry {
    page_id_t page_id;
    BPlusTreePage node;
};

//read_path()负责从根到叶子节点的遍历
bool read_path(BufferPoolManager& pool, page_id_t header_page_id, page_id_t root_page_id,
               IndexKey key, std::vector<PathEntry>& path) {
    std::unordered_set<page_id_t> visited;
    page_id_t page_id = root_page_id;
    page_id_t parent_page_id = INVALID_PAGE;
    std::optional<IndexKey> lower;
    std::optional<IndexKey> upper;
    while (page_id != header_page_id && visited.insert(page_id).second) {
        Page copy;
        BPlusTreePage node;
        if (!read_page_copy(pool, page_id, copy) || !BPlusTreePage::read_from(copy, node)
            || node.parent_page_id != parent_page_id) {
            return false;
        }
        if (node.is_leaf()) {
            if ((parent_page_id != INVALID_PAGE && node.entries.empty())
                || (parent_page_id == INVALID_PAGE && node.next_page_id != INVALID_PAGE)
                || (!node.entries.empty()
                    && ((lower && node.entries.front().key < *lower)
                        || (upper && node.entries.back().key >= *upper)))) {
                return false;
            }
            path.push_back({page_id, std::move(node)});
            return true;
        }
        if ((lower && node.keys.front() < *lower) || (upper && node.keys.back() >= *upper)) {
            return false;
        }
        const auto child_index = static_cast<std::size_t>(
            std::upper_bound(node.keys.begin(), node.keys.end(), key) - node.keys.begin());
        if (child_index > 0) {
            lower = node.keys[child_index - 1];
        }
        if (child_index < node.keys.size()) {
            upper = node.keys[child_index];
        }
        parent_page_id = page_id;
        page_id = node.children[child_index];
        path.push_back({parent_page_id, std::move(node)});
    }
    return false;
}

//利用二分查找，查找第一个大于等于所要查询的key值的key值的位置
auto find_entry(BPlusTreePage& node, IndexKey key) {
    return std::lower_bound(node.entries.begin(), node.entries.end(), key,
        [](const BPlusTreeLeafEntry& entry, IndexKey search_key) {
            return entry.key < search_key;
        });
}

bool propagate_split(PageChanges& changes, std::vector<PathEntry>& path,
                     page_id_t left_id, IndexKey separator, page_id_t right_id,
                     page_id_t& root_page_id) {
    while (!path.empty()) {
        auto parent = std::move(path.back());
        path.pop_back();
        const auto position = std::find(parent.node.children.begin(), parent.node.children.end(), left_id);
        if (position == parent.node.children.end()) {
            return false;
        }
        const auto child_index = static_cast<std::size_t>(position - parent.node.children.begin());
        parent.node.keys.insert(parent.node.keys.begin() + child_index, separator);
        parent.node.children.insert(parent.node.children.begin() + child_index + 1, right_id);
        if (parent.node.keys.size() <= BPlusTreePage::MAX_INTERNAL_KEYS) {
            return changes.stage(parent.page_id, parent.node);
        }

        const auto middle = parent.node.keys.size() / 2;
        separator = parent.node.keys[middle];
        BPlusTreePage right;
        right.type = BPlusTreeNodeType::INTERNAL;
        right.parent_page_id = parent.node.parent_page_id;
        right.keys.assign(parent.node.keys.begin() + middle + 1, parent.node.keys.end());
        right.children.assign(parent.node.children.begin() + middle + 1, parent.node.children.end());
        parent.node.keys.resize(middle);
        parent.node.children.resize(middle + 1);
        if (!changes.allocate(right_id)) {
            return false;
        }
        for (const auto child_page_id : right.children) {
            if (!changes.reparent(child_page_id, parent.page_id, right_id)) {
                return false;
            }
        }
        if (!changes.stage(parent.page_id, parent.node) || !changes.stage(right_id, right)) {
            return false;
        }
        left_id = parent.page_id;
    }

    page_id_t new_root_id = INVALID_PAGE;
    if (!changes.allocate(new_root_id)) {
        return false;
    }
    BPlusTreePage root;
    root.type = BPlusTreeNodeType::INTERNAL;
    root.keys = {separator};
    root.children = {left_id, right_id};
    if (!changes.reparent(left_id, INVALID_PAGE, new_root_id)
        || !changes.reparent(right_id, INVALID_PAGE, new_root_id)
        || !changes.stage(new_root_id, root)) {
        return false;
    }
    root_page_id = new_root_id;
    return true;
}

std::size_t node_size(const BPlusTreePage& node) {
    return node.is_leaf() ? node.entries.size() : node.keys.size();
}

std::size_t minimum_size(const BPlusTreePage& node) {
    return node.is_leaf() ? BPlusTreePage::MIN_LEAF_ENTRIES : BPlusTreePage::MIN_INTERNAL_KEYS;
}

bool subtree_minimum(PageChanges& changes, const PathEntry& current, IndexKey& minimum) {
    auto node = current.node;
    auto page_id = current.page_id;
    std::unordered_set<page_id_t> visited{page_id};
    while (!node.is_leaf()) {
        if (node.children.empty()) {
            return false;
        }
        const auto child_id = node.children.front();
        BPlusTreePage child;
        if (!visited.insert(child_id).second || !changes.read_node(child_id, child)
            || child.parent_page_id != page_id) {
            return false;
        }
        page_id = child_id;
        node = std::move(child);
    }
    if (node.entries.empty()) {
        return false;
    }
    minimum = node.entries.front().key;
    return true;
}

bool borrow_from_left(PageChanges& changes, PathEntry& current, PathEntry& left,
                       PathEntry& parent, std::size_t child_index) {
    if (current.node.is_leaf()) {
        current.node.entries.insert(current.node.entries.begin(), left.node.entries.back());
        left.node.entries.pop_back();
        parent.node.keys[child_index - 1] = current.node.entries.front().key;
    } else {
        const auto moved_child = left.node.children.back();
        current.node.keys.insert(current.node.keys.begin(), parent.node.keys[child_index - 1]);
        current.node.children.insert(current.node.children.begin(), moved_child);
        parent.node.keys[child_index - 1] = left.node.keys.back();
        left.node.keys.pop_back();
        left.node.children.pop_back();
        if (!changes.reparent(moved_child, left.page_id, current.page_id)) {
            return false;
        }
    }
    return changes.stage(left.page_id, left.node) && changes.stage(current.page_id, current.node);
}

bool borrow_from_right(PageChanges& changes, PathEntry& current, PathEntry& right,
                        PathEntry& parent, std::size_t child_index) {
    if (current.node.is_leaf()) {
        current.node.entries.push_back(right.node.entries.front());
        right.node.entries.erase(right.node.entries.begin());
        parent.node.keys[child_index] = right.node.entries.front().key;
        if (child_index > 0) {
            parent.node.keys[child_index - 1] = current.node.entries.front().key;
        }
    } else {
        const auto moved_child = right.node.children.front();
        current.node.keys.push_back(parent.node.keys[child_index]);
        current.node.children.push_back(moved_child);
        parent.node.keys[child_index] = right.node.keys.front();
        right.node.keys.erase(right.node.keys.begin());
        right.node.children.erase(right.node.children.begin());
        if (!changes.reparent(moved_child, right.page_id, current.page_id)) {
            return false;
        }
    }
    return changes.stage(right.page_id, right.node) && changes.stage(current.page_id, current.node);
}

bool merge_nodes(PageChanges& changes, PathEntry& left, const PathEntry& right, IndexKey separator) {
    if (left.node.is_leaf()) {
        left.node.entries.insert(left.node.entries.end(), right.node.entries.begin(), right.node.entries.end());
        left.node.next_page_id = right.node.next_page_id;
    } else {
        left.node.keys.push_back(separator);
        left.node.keys.insert(left.node.keys.end(), right.node.keys.begin(), right.node.keys.end());
        left.node.children.insert(left.node.children.end(), right.node.children.begin(), right.node.children.end());
        for (const auto child_id : right.node.children) {
            if (!changes.reparent(child_id, right.page_id, left.page_id)) {
                return false;
            }
        }
    }
    if (!changes.stage(left.page_id, left.node)) {
        return false;
    }
    changes.retire(right.page_id);
    return true;
}

bool repair_after_erase(PageChanges& changes, std::vector<PathEntry>& path,
                         PathEntry current, page_id_t& root_page_id) {
    while (true) {
        if (path.empty()) {
            if (!current.node.is_leaf() && current.node.keys.empty()) {
                if (current.node.children.size() != 1) {
                    return false;
                }
                const auto child_id = current.node.children.front();
                if (!changes.reparent(child_id, current.page_id, INVALID_PAGE)) {
                    return false;
                }
                changes.retire(current.page_id);
                root_page_id = child_id;
                return true;
            }
            return changes.stage(current.page_id, current.node);
        }

        auto parent = std::move(path.back());
        path.pop_back();
        const auto position = std::find(parent.node.children.begin(), parent.node.children.end(), current.page_id);
        if (position == parent.node.children.end()) {
            return false;
        }
        const auto child_index = static_cast<std::size_t>(position - parent.node.children.begin());
        if (child_index > 0 && (!current.node.is_leaf() || !current.node.entries.empty())) {
            IndexKey minimum;
            if (!subtree_minimum(changes, current, minimum)) {
                return false;
            }
            parent.node.keys[child_index - 1] = minimum;
        }

        if (node_size(current.node) >= minimum_size(current.node)) {
            if (!changes.stage(current.page_id, current.node)) {
                return false;
            }
            current = std::move(parent);
            continue;
        }

        std::optional<PathEntry> left;
        std::optional<PathEntry> right;
        auto read_sibling = [&](std::size_t sibling_index, std::optional<PathEntry>& sibling) {
            PathEntry decoded{parent.node.children[sibling_index], {}};
            if (!changes.read_node(decoded.page_id, decoded.node)
                || decoded.node.parent_page_id != parent.page_id
                || decoded.node.type != current.node.type
                || node_size(decoded.node) < minimum_size(decoded.node)) {
                return false;
            }
            sibling = std::move(decoded);
            return true;
        };
        if ((child_index > 0 && !read_sibling(child_index - 1, left))
            || (child_index + 1 < parent.node.children.size() && !read_sibling(child_index + 1, right))) {
            return false;
        }
        if (current.node.is_leaf()
            && ((left && left->node.next_page_id != current.page_id)
                || (right && current.node.next_page_id != right->page_id))) {
            return false;
        }

        if (left && node_size(left->node) > minimum_size(left->node)) {
            if (!borrow_from_left(changes, current, *left, parent, child_index)) {
                return false;
            }
        } else if (right && node_size(right->node) > minimum_size(right->node)) {
            if (!borrow_from_right(changes, current, *right, parent, child_index)) {
                return false;
            }
        } else if (left) {
            if (!merge_nodes(changes, *left, current, parent.node.keys[child_index - 1])) {
                return false;
            }
            parent.node.keys.erase(parent.node.keys.begin() + child_index - 1);
            parent.node.children.erase(parent.node.children.begin() + child_index);
        } else if (right) {
            if (!merge_nodes(changes, current, *right, parent.node.keys[child_index])) {
                return false;
            }
            parent.node.keys.erase(parent.node.keys.begin() + child_index);
            parent.node.children.erase(parent.node.children.begin() + child_index + 1);
        } else {
            return false;
        }
        current = std::move(parent);
    }
}

}

BPlusTree::BPlusTree(BufferPoolManager& buffer_pool, page_id_t header_page_id,
                     page_id_t root_page_id)
    : buffer_pool_(buffer_pool), header_page_id_(header_page_id), root_page_id_(root_page_id) {}

std::unique_ptr<BPlusTree> BPlusTree::create(BufferPoolManager& buffer_pool) {
    PageChanges changes(buffer_pool, INVALID_PAGE);
    page_id_t root_page_id = INVALID_PAGE;
    page_id_t header_page_id = INVALID_PAGE;
    if (!changes.allocate(root_page_id) || !changes.stage(root_page_id, BPlusTreePage{})
        || !changes.allocate(header_page_id)
        || !changes.stage(header_page_id, BPlusTreeHeaderPage{root_page_id})) {
        return nullptr;
    }
    auto tree = std::unique_ptr<BPlusTree>(new BPlusTree(buffer_pool, header_page_id, root_page_id));
    if (!changes.commit()) {
        return nullptr;
    }
    return tree;
}

std::unique_ptr<BPlusTree> BPlusTree::open(BufferPoolManager& buffer_pool,
                                         page_id_t header_page_id) {
    BPlusTreeHeaderPage header;
    Page copy;
    BPlusTreePage root;
    if (!read_header(buffer_pool, header_page_id, header)
        || !read_page_copy(buffer_pool, header.root_page_id, copy)
        || !BPlusTreePage::read_from(copy, root) || root.parent_page_id != INVALID_PAGE
        || (root.is_leaf() && root.next_page_id != INVALID_PAGE)) {
        return nullptr;
    }
    return std::unique_ptr<BPlusTree>(new BPlusTree(buffer_pool, header_page_id, header.root_page_id));
}

//refresh_root()，读取头页，获得根节点的页号
page_id_t BPlusTree::refresh_root() const {
    BPlusTreeHeaderPage header;
    if (!read_header(buffer_pool_, header_page_id_, header)) {
        return INVALID_PAGE;
    }
    root_page_id_ = header.root_page_id;
    return root_page_id_;
}

//插入记录
BPlusTree::InsertResult BPlusTree::insert(IndexKey key, const RID& rid) {
    if (!rid.is_valid()) {
        return InsertResult::INVALID_RID;
    }

    //更新根节点
    const auto current_root_id = refresh_root();
    //查找到叶子节点的路径
    std::vector<PathEntry> path;
    if (current_root_id == INVALID_PAGE
        || !read_path(buffer_pool_, header_page_id_, current_root_id, key, path)) {
        return InsertResult::PAGE_ERROR;
    }
    //拿到叶子节点
    auto leaf = std::move(path.back());
    path.pop_back();
    //拿到第一个大于等于key值的索引位置
    const auto position = find_entry(leaf.node, key);
    if (position != leaf.node.entries.end() && position->key == key) {
        return InsertResult::DUPLICATE_KEY;
    }
    leaf.node.entries.insert(position, {key, rid});
    PageChanges changes(buffer_pool_, header_page_id_);
    page_id_t new_root_id = current_root_id;
    if (leaf.node.entries.size() <= BPlusTreePage::MAX_ENTRIES) {
        if (!changes.stage(leaf.page_id, leaf.node)) {
            return InsertResult::PAGE_ERROR;
        }
    } else {
        const auto middle = leaf.node.entries.size() / 2;
        BPlusTreePage right;
        right.parent_page_id = leaf.node.parent_page_id;
        right.next_page_id = leaf.node.next_page_id;
        right.entries.assign(leaf.node.entries.begin() + middle, leaf.node.entries.end());
        leaf.node.entries.resize(middle);
        page_id_t right_id = INVALID_PAGE;
        if (!changes.allocate(right_id)) {
            return InsertResult::PAGE_ERROR;
        }
        leaf.node.next_page_id = right_id;
        if (!changes.stage(right_id, right) || !changes.stage(leaf.page_id, leaf.node)
            || !propagate_split(changes, path, leaf.page_id, right.entries.front().key,
                right_id, new_root_id)) {
            return InsertResult::PAGE_ERROR;
        }
    }
    if (new_root_id != current_root_id
        && !changes.stage(header_page_id_, BPlusTreeHeaderPage{new_root_id})) {
        return InsertResult::PAGE_ERROR;
    }
    if (!changes.commit()) {
        return InsertResult::PAGE_ERROR;
    }
    root_page_id_ = new_root_id;
    return InsertResult::INSERTED;
}


//find()根据key,首先要调用read_path()进行从根节点到叶子节点的遍历->返回叶子节点，然后对索引进行遍历，查找是否有key的匹配
BPlusTree::FindResult BPlusTree::find(IndexKey key, RID& rid) const {
    const auto current_root_id = refresh_root();
    std::vector<PathEntry> path;
    if (current_root_id == INVALID_PAGE
        || !read_path(buffer_pool_, header_page_id_, current_root_id, key, path)) {
        return FindResult::PAGE_ERROR;
    }
    auto& leaf = path.back().node;
    const auto position = find_entry(leaf, key);
    if (position == leaf.entries.end() || position->key != key) {
        return FindResult::NOT_FOUND;
    }
    rid = position->rid;
    return FindResult::FOUND;
}


BPlusTree::EraseResult BPlusTree::erase(IndexKey key) {
    const auto current_root_id = refresh_root();
    std::vector<PathEntry> path;
    if (current_root_id == INVALID_PAGE
        || !read_path(buffer_pool_, header_page_id_, current_root_id, key, path)) {
        return EraseResult::PAGE_ERROR;
    }
    auto leaf = std::move(path.back());
    path.pop_back();
    const auto position = find_entry(leaf.node, key);
    if (position == leaf.node.entries.end() || position->key != key) {
        return EraseResult::NOT_FOUND;
    }
    leaf.node.entries.erase(position);
    PageChanges changes(buffer_pool_, header_page_id_);
    auto new_root_id = current_root_id;
    if (!repair_after_erase(changes, path, std::move(leaf), new_root_id)
        || (new_root_id != current_root_id
            && !changes.stage(header_page_id_, BPlusTreeHeaderPage{new_root_id}))
        || !changes.commit()) {
        return EraseResult::PAGE_ERROR;
    }
    root_page_id_ = new_root_id;
    return changes.reclaim() ? EraseResult::REMOVED : EraseResult::RECLAIM_FAILED;
}

BPlusTree::ScanResult BPlusTree::scan(std::optional<IndexKey> lower,
                                     std::optional<IndexKey> upper,
                                     std::vector<BPlusTreeLeafEntry>& entries,
                                     std::size_t limit) const {
    if (lower && upper && *lower > *upper) {
        return ScanResult::INVALID_RANGE;
    }
    std::vector<BPlusTreeLeafEntry> collected;
    if (limit == 0 || (lower && upper && *lower == *upper)) {
        entries.swap(collected);
        return ScanResult::SUCCESS;
    }
    const auto current_root_id = refresh_root();
    std::vector<PathEntry> path;
    if (current_root_id == INVALID_PAGE
        || !read_path(buffer_pool_, header_page_id_, current_root_id,
            lower.value_or(std::numeric_limits<IndexKey>::min()), path)) {
        return ScanResult::PAGE_ERROR;
    }
    auto current = std::move(path.back());
    std::unordered_set<page_id_t> visited{current.page_id};
    while (true) {
        const auto first = lower ? find_entry(current.node, *lower) : current.node.entries.begin();
        for (auto position = first; position != current.node.entries.end(); ++position) {
            if (upper && position->key >= *upper) {
                entries.swap(collected);
                return ScanResult::SUCCESS;
            }
            collected.push_back(*position);
            if (collected.size() == limit) {
                entries.swap(collected);
                return ScanResult::SUCCESS;
            }
        }
        const auto next_id = current.node.next_page_id;
        if (next_id == INVALID_PAGE) {
            entries.swap(collected);
            return ScanResult::SUCCESS;
        }
        if (next_id == header_page_id_ || !visited.insert(next_id).second
            || current.node.entries.empty()) {
            return ScanResult::PAGE_ERROR;
        }
        Page copy;
        BPlusTreePage next;
        if (!read_page_copy(buffer_pool_, next_id, copy) || !BPlusTreePage::read_from(copy, next)
            || !next.is_leaf() || next.entries.empty() || next.parent_page_id == INVALID_PAGE
            || next.entries.front().key <= current.node.entries.back().key) {
            return ScanResult::PAGE_ERROR;
        }
        current = {next_id, std::move(next)};
    }
}
