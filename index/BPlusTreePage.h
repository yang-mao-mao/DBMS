#ifndef INDEX_B_PLUS_TREE_PAGE_H
#define INDEX_B_PLUS_TREE_PAGE_H

#include "RID.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using IndexKey = std::int64_t;

//枚举B+树结点的种类，可能是叶子节点也可能是内部节点
enum class BPlusTreeNodeType : std::uint16_t {
    LEAF = 1,
    INTERNAL = 2
};

//这个数据结构保存key到RID的映射关系
struct BPlusTreeLeafEntry {
    IndexKey key{};
    RID rid{};

    bool operator==(const BPlusTreeLeafEntry&) const = default;
};

struct BPlusTreePage {

    //定义这个结构体的约束常量
    //头的大小
    static constexpr std::size_t HEADER_SIZE = 20;
    //一个叶子项（key,RID）编码后的长度
    static constexpr std::size_t ENTRY_SIZE = 14;
    //一个叶子最多保存的索引数量
    static constexpr std::size_t MAX_ENTRIES =
        (PAGE_SIZE - sizeof(PageHeader) - sizeof(Slot) - HEADER_SIZE) / ENTRY_SIZE;
    //一个内部键的长度和右侧子页号的编码长度
    static constexpr std::size_t INTERNAL_ENTRY_SIZE = 12;
    //一个内部节点最多能保存的分隔键数
    static constexpr std::size_t MAX_INTERNAL_KEYS =
        (PAGE_SIZE - sizeof(PageHeader) - sizeof(Slot) - HEADER_SIZE) / INTERNAL_ENTRY_SIZE;
    //非根叶子节点最小项数
    static constexpr std::size_t MIN_LEAF_ENTRIES = (MAX_ENTRIES + 1) / 2;
    //非根内部节点最小键数
    static constexpr std::size_t MIN_INTERNAL_KEYS = MAX_INTERNAL_KEYS / 2;

    BPlusTreeNodeType type{BPlusTreeNodeType::LEAF};
    std::uint32_t parent_page_id{INVALID_PAGE};
    //叶子结点的下一页页号，叶子节点保存的有序索引项
    std::uint32_t next_page_id{INVALID_PAGE};
    std::vector<BPlusTreeLeafEntry> entries;
    //内部节点的key值，内部节点的子节点的页号
    std::vector<IndexKey> keys;
    std::vector<std::uint32_t> children;

    [[nodiscard]] bool is_leaf() const { return type == BPlusTreeNodeType::LEAF; }
    [[nodiscard]] bool is_valid() const;
    [[nodiscard]] bool write_to(Page& page) const;
    [[nodiscard]] static bool read_from(const Page& page, BPlusTreePage& node);

    bool operator==(const BPlusTreePage&) const = default;
};

//主要保存当前根节点在哪一页，因为构建和更新B+树的过程中，根节点会发生变化，所以需要较为持久的存储
/*
* 一个完整的头页，4096字节
┌───────────────────────────────┐
│ 通用 PageHeader：48字节       │
├───────────────────────────────┤
│ 槽0：6字节                    │
├───────────────────────────────┤
│                               │
│ 空闲空间                      │
│                               │
├───────────────────────────────┤
│ 头页记录：12字节               │
│ ┌───────────────────────────┐ │
│ │ 魔数：4字节               │ │
│ │ 版本：2字节               │ │
│ │ 记录类型：2字节，值为0    │ │
│ │ root_page_id：4字节       │ │
│ └───────────────────────────┘ │
└───────────────────────────────┘
 */
struct BPlusTreeHeaderPage {
    static constexpr std::size_t RECORD_SIZE = 12;

    std::uint32_t root_page_id{INVALID_PAGE};

    [[nodiscard]] bool write_to(Page& page) const;
    [[nodiscard]] static bool read_from(const Page& page, BPlusTreeHeaderPage& header);
};

#endif
