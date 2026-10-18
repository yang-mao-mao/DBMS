#include "ci_pan_cun_chu.h"
#include <cstddef>

#include <limits>
//写一个函数，用于在磁盘上创建文件，打开文件，返回值为bool
#include<cstring>
#include <fstream>
#include<memory>
#include<cstdint>
#include <iostream>
#include <vector>
#include <algorithm>

using namespace std;

namespace {

bool slot_directory_is_valid(const Page& page) {
    const std::size_t directory_end = sizeof(PageHeader)
        + static_cast<std::size_t>(page.header.slot_count) * sizeof(Slot);

    return page.header.magic == PAGE_MAGIC
        && page.header.page_size == PAGE_SIZE
        && directory_end <= PAGE_SIZE
        && page.header.free_start == directory_end
        && page.header.free_start <= page.header.free_end
        && page.header.free_end <= PAGE_SIZE;
}

Slot load_slot_unchecked(const Page& page, std::uint16_t index) {
    Slot slot{};
    std::memcpy(&slot, reinterpret_cast<const char*>(&page)
        + sizeof(PageHeader) + static_cast<std::size_t>(index) * sizeof(Slot),
        sizeof(Slot));
    return slot;
}

void store_slot_unchecked(Page& page, std::uint16_t index, const Slot& slot) {
    std::memcpy(reinterpret_cast<char*>(&page)
        + sizeof(PageHeader) + static_cast<std::size_t>(index) * sizeof(Slot),
        &slot, sizeof(Slot));
}

bool record_bounds_are_valid(const Page& page, const Slot& slot) {
    const std::size_t offset = slot.offset;
    const std::size_t length = slot.length;
    return length != 0
        && offset >= page.header.free_end
        && offset >= page.header.free_start
        && offset <= PAGE_SIZE
        && length <= PAGE_SIZE - offset;
}

bool page_type_is_valid(PageType type) {
    switch (type) {
        case PageType::META:
        case PageType::DATA:
        case PageType::INDEX:
        case PageType::OVERFLOW:
        case PageType::FREE:
            return true;
        case PageType::INVALID:
        default:
            return false;
    }
}

bool active_records_are_valid(const Page& page) {
    if (!slot_directory_is_valid(page)) {
        return false;
    }

    std::size_t live_bytes = 0;
    std::size_t deleted_record_bytes = 0;
    for (std::uint16_t i = 0; i < page.header.slot_count; ++i) {
        const Slot current = load_slot_unchecked(page, i);
        if ((current.flags & static_cast<std::uint16_t>(~SLOT_FLAG_DELETED)) != 0) {
            return false;
        }
        const bool deleted = (current.flags & SLOT_FLAG_DELETED) != 0;
        if (deleted && current.length == 0) {
            if (current.offset != 0) {
                return false;
            }
        } else {
            if (!record_bounds_are_valid(page, current)) {
                return false;
            }
            if (deleted) {
                deleted_record_bytes += current.length;
            } else {
                live_bytes += current.length;
            }
        }
        if (live_bytes > PAGE_SIZE - page.header.free_start) {
            return false;
        }

        if (current.length == 0) {
            continue;
        }
        const std::size_t current_end =
            static_cast<std::size_t>(current.offset) + current.length;
        for (std::uint16_t j = 0; j < i; ++j) {
            const Slot previous = load_slot_unchecked(page, j);
            if (previous.length == 0) {
                continue;
            }
            const std::size_t previous_end =
                static_cast<std::size_t>(previous.offset) + previous.length;
            if (current.offset < previous_end && previous.offset < current_end) {
                return false;
            }
        }
    }
    const std::size_t occupied_bytes = PAGE_SIZE - page.header.free_end;
    return live_bytes <= occupied_bytes
        && deleted_record_bytes <= page.header.fragment_bytes
        && page.header.fragment_bytes == occupied_bytes - live_bytes;
}

bool stabilize_page_source(const Page& page, const char*& source,
                           std::uint16_t length, std::vector<char>& copy) {
    const auto page_begin = reinterpret_cast<std::uintptr_t>(&page);
    const auto page_end = page_begin + sizeof(Page);
    const auto source_begin = reinterpret_cast<std::uintptr_t>(source);
    if (source_begin > std::numeric_limits<std::uintptr_t>::max() - length) {
        return false;
    }
    const auto source_end = source_begin + length;
    const bool overlaps_page = source_begin < page_end && source_end > page_begin;
    if (!overlaps_page) {
        return true;
    }
    if (source_begin < page_begin || source_end > page_end) {
        return false;
    }

    copy.assign(source, source + length);
    source = copy.data();
    return true;
}

} // namespace


//数据库底层不能使用文本文件，应该创建二进制文件，读写文件也使用二进制流


//创建新文件，倘若文件已存在，则截断
bool create_file(const std::string& name) {
    std::ofstream file(name, std::ios::binary | std::ios::out | std::ios::trunc);
    return file.is_open();
}


//根据文件名打开文件并检查文件是否正常打开的方法：
//auto f = open_file("test.txt");
//if
//(f) {
//    *f << "hello\n";
//}

//以只读模式打开文件
unique_ptr<ifstream> open_read(const string& name) {
    auto f = make_unique<ifstream>(name, ios::binary | ios::in);
    if (!f->is_open()) return nullptr;
    return f;
}

//以读写模式打开文件
unique_ptr<fstream> open_rw(const string& name) {
    auto f = make_unique<fstream>(name, ios::binary | ios::in | ios::out);
    if (!f->is_open()) return nullptr;
    return f;
}

// 创建并读写（截断）
unique_ptr<fstream> create_rw(const string& name) {
    auto f = make_unique<fstream>(
        name, ios::binary | ios::in | ios::out | ios::trunc);
    if (!f->is_open()) return nullptr;
    return f;
}

// 打开（不存在则创建），可读可写，不截断
unique_ptr<fstream> open_file(const string& name) {
    auto f = make_unique<fstream>(name, ios::binary | ios::in | ios::out);
    if (!f->is_open()) {
        f = make_unique<fstream>(
            name, ios::binary | ios::in | ios::out | ios::trunc);
    }
    if (!f->is_open()) return nullptr;
    return f;
}





// // ---- 页间链接 (8 字节) ----
// uint32_t prev_page;      // 上一页页号，0xFFFFFFFF 表示无
// uint32_t next_page;      // 下一页页号，0xFFFFFFFF 表示无
//
// // ---- 完整性校验 (8 字节) ----
// uint32_t checksum;       // 整页校验和（计算时本字段置 0）
// uint32_t reserved;       // 预留，凑齐对齐或未来扩展

void Page::init(std::uint32_t page_id, PageType type) {
    //清零整个页面,将所有的字节都置为0
    std::memset(static_cast<void*>(this), 0, sizeof(*this));
    //初始化页头
    header.magic = PAGE_MAGIC;//填充魔数，表明这个页是数据库的页
    header.page_size = PAGE_SIZE;
    header.page_id = page_id;
    header.page_type = type;
    header.file_version = PAGE_FORMAT_VERSION;
    header.lsn= 0 ;//目前还没有实现日志系统，以0作为标记

    header.slot_count = 0;
    header.free_start = sizeof(PageHeader);
    header.free_end = PAGE_SIZE;//4096也行
    header.fragment_bytes = 0;

    header.prev_page  = INVALID_PAGE;
    header.next_page  = INVALID_PAGE;

    //校验应该怎么做？
    header.reserved   = 0;
    header.checksum   = 0;
    update_checksum();
}   // 初始化空页

void Page::clear() {
    // 保留页的身份及链接信息，只清空槽目录和记录区。
    std::memset(body, 0, sizeof(body));
    header.slot_count = 0;
    header.free_start = sizeof(PageHeader);
    header.free_end = PAGE_SIZE;
    header.fragment_bytes = 0;

    //校验和
    update_checksum();
}

int  Page::insert_record(const char* record, std::uint16_t length) {

    //先做参数检查
    if (length == 0) return -1;
    if (record == nullptr) return -1;
    if (!active_records_are_valid(*this)) return -1;

    // compact 可能移动页内记录，因此页内来源必须先保存。
    std::vector<char> source_copy;
    if (!stabilize_page_source(*this, record, length, source_copy)) return -1;

    //空间不够时的处理方案
    // if (length > PAGE_SIZE) {
    //     cout << "Page is too big" << endl;
    //     return -1;
    // }无意义，一条记录不可能比页还大，否则无论如何都无法存储

    const std::size_t required = static_cast<std::size_t>(length) + sizeof(Slot);
    if (required > free_bytes()) {
        compact();
        if (required > free_bytes()) {
            return -1;
        }
    }

    //写入记录
    const uint16_t record_offset =
        static_cast<uint16_t>(header.free_end - length);
    memcpy(reinterpret_cast<char*>(this) + record_offset, record, length);

    //写入槽
    Slot slot;
    slot.flags=0;
    slot.length = length;
    slot.offset = record_offset;
    memcpy(reinterpret_cast<char*>(this) + header.free_start, &slot, sizeof(Slot));

    //修改页头
    header.slot_count ++;
    header.free_start +=sizeof(Slot);
    header.free_end -= length;

    update_checksum();
    return header.slot_count-1;//返回槽号不是槽总数
}

const char* Page::get_record(std::uint16_t slot_index, std::uint16_t& out_len) const {
    out_len = 0;
    if (!slot_directory_is_valid(*this) || slot_index >= header.slot_count) {
        return nullptr;
    }
    const Slot slot = load_slot_unchecked(*this, slot_index);
    if ((slot.flags & SLOT_FLAG_DELETED) != 0) {
        return nullptr;
    }
    if ((slot.flags & static_cast<std::uint16_t>(~SLOT_FLAG_DELETED)) != 0
        || !record_bounds_are_valid(*this, slot)) {
        return nullptr;
    }

    const std::size_t record_end =
        static_cast<std::size_t>(slot.offset) + slot.length;
    for (std::uint16_t i = 0; i < header.slot_count; ++i) {
        if (i == slot_index) continue;
        const Slot other = load_slot_unchecked(*this, i);
        if (other.length == 0) continue;
        if (!record_bounds_are_valid(*this, other)) return nullptr;
        const std::size_t other_end =
            static_cast<std::size_t>(other.offset) + other.length;
        if (slot.offset < other_end && other.offset < record_end) {
            return nullptr;
        }
    }

    out_len = slot.length;
    return reinterpret_cast<const char*>(this) + slot.offset;
}

bool Page::is_deleted(std::uint16_t slot_index) const {
    //越界，说明槽应该是不存在的
    if (!slot_directory_is_valid(*this) || slot_index >= header.slot_count) return true;
    const char* slot_addr = reinterpret_cast<const char*>(this)
                         + sizeof(PageHeader)
                         + static_cast<std::size_t>(slot_index) * sizeof(Slot);
    //为结构体赋值，也可以直接对着字节操作
    Slot slot;
    memcpy(&slot, slot_addr, sizeof(Slot));
    return (slot.flags & SLOT_FLAG_DELETED) != 0;
}

// ==================== 更新 ====================
// 新长度 <= 旧长度：原地改
// 新长度 >  旧长度：尝试搬移，失败返回 false
Slot Page::load_slot(std::uint16_t i) const {
    if (!slot_directory_is_valid(*this) || i >= header.slot_count) {
        return Slot{0, 0, SLOT_FLAG_DELETED};
    }
    return load_slot_unchecked(*this, i);
}

void Page::store_slot(std::uint16_t i, const Slot& s) {
    if (!slot_directory_is_valid(*this) || i >= header.slot_count) {
        return;
    }
    store_slot_unchecked(*this, i, s);
}
bool Page::update_record(std::uint16_t slot_index,const char* record, std::uint16_t length) {

    //倘若槽越界
    if (!active_records_are_valid(*this) || slot_index >= header.slot_count) return false;
    if(record == nullptr||length == 0) return false;

    //正常读取
    Slot slot=load_slot(slot_index);

    //检查槽是否被标记为删除
    if((slot.flags & SLOT_FLAG_DELETED) != 0) {
        return false;
    }

    // record 可能指向本页：先拷到临时 buffer，避免搬移时源被覆盖
    vector<char> tmp;
    if (!stabilize_page_source(*this, record, length, tmp)) return false;

    //根据长度进行分类
    if(length<=slot.length) {
        const std::uint16_t released =
            static_cast<std::uint16_t>(slot.length - length);
        memcpy(reinterpret_cast<char*>(this) + slot.offset, record, length);
        slot.length = length;
        store_slot(slot_index, slot);
        header.fragment_bytes = static_cast<std::uint16_t>(
            header.fragment_bytes + released);
        update_checksum();
        return true;
    }

    // 增长时旧记录本身也可回收；不能只拿新长度与当前连续空闲区比较。
    std::size_t other_live_bytes = 0;
    for (std::uint16_t i = 0; i < header.slot_count; ++i) {
        if (i == slot_index) continue;
        const Slot other = load_slot_unchecked(*this, i);
        if ((other.flags & SLOT_FLAG_DELETED) == 0) {
            other_live_bytes += other.length;
        }
    }
    if (other_live_bytes + length > PAGE_SIZE - header.free_start) {
        return false;
    }

    const std::uint16_t original_flags = slot.flags;
    slot.flags |= SLOT_FLAG_DELETED;
    store_slot_unchecked(*this, slot_index, slot);
    header.fragment_bytes = static_cast<std::uint16_t>(
        header.fragment_bytes + slot.length);
    compact();

    const std::uint16_t new_offset =
        static_cast<std::uint16_t>(header.free_end - length);
    memcpy(reinterpret_cast<char*>(this) + new_offset, record, length);
    slot.length = length;
    slot.offset = new_offset;
    slot.flags = original_flags;
    store_slot_unchecked(*this, slot_index, slot);
    header.free_end = new_offset;
    header.fragment_bytes = 0;
    update_checksum();
    return true;
}

bool Page::delete_record(std::uint16_t slot_index) {
    if (!active_records_are_valid(*this) || slot_index >= header.slot_count) return false;
    Slot slot=load_slot(slot_index);
    if ((slot.flags & SLOT_FLAG_DELETED) != 0) return false;
    slot.flags |= SLOT_FLAG_DELETED;
    store_slot(slot_index, slot);

    //差点忘了维护fragment_bytes字段，这个字段用于记录“已删除但未回收的小空洞”
    header.fragment_bytes = static_cast<std::uint16_t>(
        header.fragment_bytes + slot.length);

    update_checksum();
    return true;
}

void Page::compact() {
    if (!active_records_are_valid(*this)) {
        return;
    }

    // 先保存整页，避免移动一个记录时覆盖另一个尚未读取的记录。
    std::array<char, PAGE_SIZE> snapshot{};
    std::memcpy(snapshot.data(), this, PAGE_SIZE);

    std::size_t new_free_end = PAGE_SIZE;
    for (std::uint16_t i = 0; i < header.slot_count; ++i) {
        Slot slot = load_slot_unchecked(*this, i);
        if ((slot.flags & SLOT_FLAG_DELETED) != 0) {
            slot.offset = 0;
            slot.length = 0;
            store_slot_unchecked(*this, i, slot);
            continue;
        }

        new_free_end -= slot.length;
        std::memcpy(reinterpret_cast<char*>(this) + new_free_end,
                    snapshot.data() + slot.offset, slot.length);
        slot.offset = static_cast<std::uint16_t>(new_free_end);
        store_slot_unchecked(*this, i, slot);
    }

    header.free_end = static_cast<std::uint16_t>(new_free_end);
    header.fragment_bytes = 0;
    std::memset(reinterpret_cast<char*>(this) + header.free_start, 0,
                header.free_end - header.free_start);
    update_checksum();
}

void Page::purge_deleted_slots() {
    if (!active_records_are_valid(*this)) {
        return;
    }

    // 1) 先回收数据区：已删除记录的数据 + 碎片
    compact();   // 此时槽索引不变

    // 2) 再压缩槽目录：把有效槽前移，移除已删除槽
    std::uint16_t new_count = 0;
    for (std::uint16_t i = 0; i < header.slot_count; ++i) {
        Slot s = load_slot(i);
        if ((s.flags & SLOT_FLAG_DELETED) != 0) {
            continue;   // 丢弃已删除槽
        }
        if (new_count != i) {
            store_slot(new_count, s);   // 有效槽前移
        }
        ++new_count;
    }

    const std::uint16_t old_free_start = header.free_start;
    header.slot_count = new_count;
    header.free_start = static_cast<std::uint16_t>(
        sizeof(PageHeader) + static_cast<std::size_t>(new_count) * sizeof(Slot));
    std::memset(reinterpret_cast<char*>(this) + header.free_start, 0,
                old_free_start - header.free_start);
    header.fragment_bytes = 0;
    update_checksum();
}


std::uint32_t Page::compute_checksum() const {
    const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(this);
    // checksum 字段在页内的偏移
    const std::size_t checksum_off =
        offsetof(Page, header) + offsetof(PageHeader, checksum);

    std::uint32_t h = 2166136261u;              // FNV offset basis
    for (std::size_t i = 0; i < sizeof(*this); ++i) {
        // checksum 字段视为 0
        const std::uint8_t byte =
            (i >= checksum_off && i < checksum_off + sizeof(std::uint32_t))
                ? 0u
                : p[i];
        h ^= byte;
        h *= 16777619u;                         // FNV prime
    }
    return h;
}

void Page::update_checksum() {
    header.checksum = compute_checksum();       // compute 内部已把本字段当 0
}

bool Page::verify_checksum() const {
    return header.checksum == compute_checksum();
}

bool Page::validate() const {
    return active_records_are_valid(*this)
        && header.file_version == PAGE_FORMAT_VERSION
        && page_type_is_valid(header.page_type)
        && verify_checksum();
}

namespace {

bool page_offset(std::uint64_t page_no, std::streamoff& offset) {
    constexpr auto max_offset = std::numeric_limits<std::streamoff>::max();
    const auto page_size = static_cast<std::uint64_t>(PAGE_SIZE);
    const auto max_offset_unsigned = static_cast<std::uint64_t>(max_offset);

    // 不仅页起始偏移，整页的最后一个字节也必须能由 streamoff 表示。
    if (page_no > std::numeric_limits<std::uint32_t>::max()
        || max_offset_unsigned < page_size - 1
        || page_no > (max_offset_unsigned - (page_size - 1)) / page_size) {
        return false;
    }

    offset = static_cast<std::streamoff>(page_no * page_size);
    return true;
}

bool page_identity_is_valid(const Page& page, std::uint64_t page_no) {
    return page_no <= std::numeric_limits<std::uint32_t>::max()
        && page.header.page_id == static_cast<std::uint32_t>(page_no)
        && page.validate();
}

} // namespace

bool write_page(std::fstream& file, std::uint64_t page_no, const Page& page) {
    std::streamoff offset{};
    if (!file.is_open()
        || !page_offset(page_no, offset)
        || !page_identity_is_valid(page, page_no)) {
        return false;
    }

    file.clear();
    file.seekp(offset, std::ios::beg);
    if (!file) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(&page),
               static_cast<std::streamsize>(sizeof(Page)));
    if (!file) {
        return false;
    }

    file.flush();
    return file.good();
}

bool read_page(std::fstream& file, std::uint64_t page_no, Page& page) {
    std::streamoff offset{};
    if (!file.is_open() || !page_offset(page_no, offset)) {
        return false;
    }

    file.clear();
    file.seekg(offset, std::ios::beg);
    if (!file) {
        return false;
    }

    // 先读入临时页。任何短读或校验失败都不能污染调用者的输出参数。
    Page loaded{};
    file.read(reinterpret_cast<char*>(&loaded),
              static_cast<std::streamsize>(sizeof(Page)));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(Page))) {
        return false;
    }
    if (!page_identity_is_valid(loaded, page_no)) {
        return false;
    }

    page = loaded;
    return true;
}
