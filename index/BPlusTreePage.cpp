#include "BPlusTreePage.h"

#include <bit>
#include <algorithm>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace {

constexpr std::uint32_t INDEX_MAGIC = 0x42505449u;
constexpr std::uint16_t INDEX_VERSION = 1;
constexpr std::uint16_t HEADER_PAGE_KIND = 0;


//固定按照小端序。给定一个无符号整数，将其拆分成若干字节，追加到bytes后部
template <typename Integer>
void append_integer(std::vector<char>& bytes, Integer value) {
    static_assert(std::is_unsigned_v<Integer>);
    auto remaining = static_cast<std::uint64_t>(value);
    for (std::size_t byte_index = 0; byte_index < sizeof(Integer); ++byte_index) {
        bytes.push_back(static_cast<char>(remaining & 0xFFu));
        remaining >>= 8;
    }
}

//将指定的bytes读取成无符号整数,offset记录游标位置，传入时是读取开始位置，函数调用结束后是读取结束位置
template <typename Integer>
Integer read_integer(const char* bytes, std::size_t& offset) {
    static_assert(std::is_unsigned_v<Integer>);
    std::uint64_t value = 0;
    for (std::size_t byte_index = 0; byte_index < sizeof(Integer); ++byte_index) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[offset++]))
            << (byte_index * 8);
    }
    return static_cast<Integer>(value);
}

void append_prefix(std::vector<char>& bytes, std::uint16_t kind) {
    append_integer(bytes, INDEX_MAGIC);
    append_integer(bytes, INDEX_VERSION);
    append_integer(bytes, kind);
}

bool read_prefix(const char* bytes, std::size_t& offset, std::uint16_t kind) {
    const auto magic = read_integer<std::uint32_t>(bytes, offset);
    const auto version = read_integer<std::uint16_t>(bytes, offset);
    const auto stored_kind = read_integer<std::uint16_t>(bytes, offset);
    return magic == INDEX_MAGIC && version == INDEX_VERSION && stored_kind == kind;
}

bool valid_index_page(const Page& page) {
    return page.header.page_type == PageType::INDEX
        && page.header.page_id != 0 && page.header.page_id != INVALID_PAGE
        && page.validate();
}


//read_payload()负责读取记录，因为Page用于B+树时，只有一条记录，所以调用Page.get_record()，返回指向第零条记录开头的只读指针以及这条记录的长度
const char* read_payload(const Page& page, std::uint16_t& length) {
    if (!valid_index_page(page) || page.slot_count() != 1) {
        return nullptr;
    }
    return page.get_record(0, length);
}

//写记录，复制一份传入的页，将复制体的记录清空（保留页号等），将字节流写入Page，将复制体赋给传入的指针
bool write_payload(Page& page, const std::vector<char>& bytes) {
    if (!valid_index_page(page) || page.slot_count() > 1
        || bytes.empty() || bytes.size() > PAGE_SIZE - sizeof(PageHeader) - sizeof(Slot)) {
        return false;
    }
    Page updated = page;
    updated.clear();
    if (updated.insert_record(bytes.data(), static_cast<std::uint16_t>(bytes.size())) != 0) {
        return false;
    }
    page = updated;
    return true;
}

}


//对叶子节点进行合法检查，注意叶子节点的关键数据结构vector<(key,RID)> entries
bool BPlusTreePage::is_valid() const {
    if (parent_page_id == 0 || next_page_id == 0) {
        return false;
    }
    if (!is_leaf()) {
        if (type != BPlusTreeNodeType::INTERNAL || !entries.empty()
            || next_page_id != INVALID_PAGE || keys.empty() || keys.size() > MAX_INTERNAL_KEYS
            || children.size() != keys.size() + 1) {
            return false;
        }
        if (std::adjacent_find(keys.begin(), keys.end(),
                [](IndexKey left, IndexKey right) { return left >= right; }) != keys.end()) {
            return false;
        }
        std::unordered_set<std::uint32_t> unique_children;
        for (const auto child_page_id : children) {
            if (child_page_id == 0 || child_page_id == INVALID_PAGE
                || child_page_id == parent_page_id || !unique_children.insert(child_page_id).second) {
                return false;
            }
        }
        return true;
    }
    if (entries.size() > MAX_ENTRIES || !keys.empty() || !children.empty()) {
        return false;
    }
    for (std::size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
        if (!entries[entry_index].rid.is_valid()
            || (entry_index > 0 && entries[entry_index - 1].key >= entries[entry_index].key)) {
            return false;
        }
    }
    return true;
}

/*
* 低地址，页内偏移 0
┌──────────────────────────────────┐
│ PageHeader：48字节                │
│ 页号、页类型、槽数量、校验和等    │
├──────────────────────────────────┤ ← 偏移48
│ 槽0：6字节                       │
│ offset、length、flags            │
├──────────────────────────────────┤ ← 偏移54
│                                  │
│             空闲空间             │
│                                  │
├──────────────────────────────────┤ ← 槽0的offset
│                                  │
│ 一整条B+树节点记录，也就是bytes   │
│                                  │
└──────────────────────────────────┘ ← 页面末尾4096
|  |  |  |  |
bytes
┌──────────────────────────────┐
│ B+树节点头部：20字节          │
├──────────────────────────────┤
│ 叶子项，或者内部节点路由项    │
└──────────────────────────────┘
|---|---|---|---|
| 0～3 | 4 字节 | 索引魔数 | append_prefix() 内部写入 |
| 4～5 | 2 字节 | 格式版本 | append_prefix() 内部写入 |
| 6～7 | 2 字节 | 节点类型 | append_prefix() 内部写入 |
| 8～9 | 2 字节 | 叶子项数或分隔键数 | append_integer(...count) |
| 10～11 | 2 字节 | 保留字段，当前为 0 | append_integer(...uint16_t{0}) |
| 12～15 | 4 字节 | 父节点页号 | append_integer(...parent_page_id) |
| 16～19 | 4 字节 | 后继叶子页号，或第一个子节点页号 | 最后一个头部字段 |
 */

//将BPlusTreePage的内容写入bytes,然后将bytes作为一整条记录写入磁盘页
bool BPlusTreePage::write_to(Page& page) const {
    //检查这一页是否合法
    if (!is_valid() || parent_page_id == page.header.page_id
        || next_page_id == page.header.page_id
        || std::find(children.begin(), children.end(), page.header.page_id) != children.end()) {
        return false;
    }

    //声明一个bytes并预留空间
    std::vector<char> bytes;
    const auto count = is_leaf() ? entries.size() : keys.size();
    bytes.reserve(HEADER_SIZE + count * (is_leaf() ? ENTRY_SIZE : INTERNAL_ENTRY_SIZE));

    append_prefix(bytes, static_cast<std::uint16_t>(type));
    append_integer(bytes, static_cast<std::uint16_t>(count));
    append_integer(bytes, std::uint16_t{0});
    append_integer(bytes, parent_page_id);
    append_integer(bytes, is_leaf() ? next_page_id : children.front());
    for (const auto& entry : entries) {
        append_integer(bytes, std::bit_cast<std::uint64_t>(entry.key));
        append_integer(bytes, entry.rid.page_id);
        append_integer(bytes, entry.rid.slot_id);
    }
    for (std::size_t key_index = 0; key_index < keys.size(); ++key_index) {
        append_integer(bytes, std::bit_cast<std::uint64_t>(keys[key_index]));
        append_integer(bytes, children[key_index + 1]);
    }
    return write_payload(page, bytes);
}

//write_to()的反向操作而已
bool BPlusTreePage::read_from(const Page& page, BPlusTreePage& node) {
    std::uint16_t length = 0;
    const char* bytes = read_payload(page, length);
    if (bytes == nullptr || length < HEADER_SIZE) {
        return false;
    }

    std::size_t offset = 0;
    const auto magic = read_integer<std::uint32_t>(bytes, offset);
    const auto version = read_integer<std::uint16_t>(bytes, offset);
    const auto kind = read_integer<std::uint16_t>(bytes, offset);
    if (magic != INDEX_MAGIC || version != INDEX_VERSION
        || (kind != static_cast<std::uint16_t>(BPlusTreeNodeType::LEAF)
            && kind != static_cast<std::uint16_t>(BPlusTreeNodeType::INTERNAL))) {
        return false;
    }
    const auto count = read_integer<std::uint16_t>(bytes, offset);
    const auto reserved = read_integer<std::uint16_t>(bytes, offset);
    const bool leaf = kind == static_cast<std::uint16_t>(BPlusTreeNodeType::LEAF);
    if (reserved != 0 || count > (leaf ? MAX_ENTRIES : MAX_INTERNAL_KEYS)
        || length != HEADER_SIZE + count * (leaf ? ENTRY_SIZE : INTERNAL_ENTRY_SIZE)) {
        return false;
    }

    BPlusTreePage decoded;
    decoded.type = static_cast<BPlusTreeNodeType>(kind);
    decoded.parent_page_id = read_integer<std::uint32_t>(bytes, offset);
    const auto link_page_id = read_integer<std::uint32_t>(bytes, offset);
    if (leaf) {
        decoded.next_page_id = link_page_id;
        decoded.entries.reserve(count);
        for (std::size_t entry_index = 0; entry_index < count; ++entry_index) {
            const auto key_bits = read_integer<std::uint64_t>(bytes, offset);
            const auto page_id = read_integer<std::uint32_t>(bytes, offset);
            const auto slot_id = read_integer<std::uint16_t>(bytes, offset);
            decoded.entries.push_back({std::bit_cast<IndexKey>(key_bits), {page_id, slot_id}});
        }
    } else {
        decoded.children.push_back(link_page_id);
        decoded.keys.reserve(count);
        decoded.children.reserve(static_cast<std::size_t>(count) + 1);
        for (std::size_t key_index = 0; key_index < count; ++key_index) {
            const auto key_bits = read_integer<std::uint64_t>(bytes, offset);
            decoded.keys.push_back(std::bit_cast<IndexKey>(key_bits));
            decoded.children.push_back(read_integer<std::uint32_t>(bytes, offset));
        }
    }
    if (!decoded.is_valid() || decoded.parent_page_id == page.header.page_id
        || decoded.next_page_id == page.header.page_id
        || std::find(decoded.children.begin(), decoded.children.end(), page.header.page_id)
            != decoded.children.end()) {
        return false;
    }
    node = std::move(decoded);
    return true;
}

//处理头页的写入和读取，头页保存根节点
bool BPlusTreeHeaderPage::write_to(Page& page) const {
    if (root_page_id == 0 || root_page_id == INVALID_PAGE || root_page_id == page.header.page_id) {
        return false;
    }
    std::vector<char> bytes;
    bytes.reserve(RECORD_SIZE);
    append_prefix(bytes, HEADER_PAGE_KIND);
    append_integer(bytes, root_page_id);
    return write_payload(page, bytes);
}

bool BPlusTreeHeaderPage::read_from(const Page& page, BPlusTreeHeaderPage& header) {
    std::uint16_t length = 0;
    const char* bytes = read_payload(page, length);
    if (bytes == nullptr || length != RECORD_SIZE) {
        return false;
    }
    std::size_t offset = 0;
    if (!read_prefix(bytes, offset, HEADER_PAGE_KIND)) {
        return false;
    }
    const auto root_page_id = read_integer<std::uint32_t>(bytes, offset);
    if (root_page_id == 0 || root_page_id == INVALID_PAGE || root_page_id == page.header.page_id) {
        return false;
    }
    header.root_page_id = root_page_id;
    return true;
}


/*
 * 让我总结一下我从BPlusTreePage.cpp中学到的东西。
 * 首先是整体的B+树布局，由一个头页引出根节点，根节点指向内部节点，最后以叶子节点为收尾。
 * 头页在内存中对应BPlusTreeHeaderPage数据结构，节点对应BPlusTreePage(叶子节点和内部节点的数据成员不同)。
 * 这些数据结构可以与定义好的Page数据结构（4096字节）进行转换，包含固定的Page头(48)，一个Slot(6)。然后body部分只存一条记录，
 * 这条记录里存的内容包含B+树某数据结构的全部信息。
 * 对于叶子节点和内部节点，写入一个头部，然后循环写入(key,RID)或者(key,Page_id);
 * 对于头页，只要把那12字节写入就行。
 * 从微观角度来看，这里提到的所有写入和读取操作，使用bytes字节流进行操作。
 * 因为本质上都是写入一条记录，所以可以调用Page.insert_record(bytes)，所以待实现的问题就是怎么构造bytes。
 * 需要使用编码的方式，对于B+树数据结构的各数据成员（基本都是无符号整数类型），将它们按照小端序，一个字节一个字节地写入bytes。读取操作与写入操作相反
 */