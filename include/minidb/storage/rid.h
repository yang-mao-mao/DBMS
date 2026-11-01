#ifndef MINIDB_STORAGE_RID_H
#define MINIDB_STORAGE_RID_H

#include <compare>
#include <cstdint>

namespace minidb {

// 成员 A 的存储层没有独立 RID 类型，因此适配层用“页号 + 槽号”表示记录位置。
// RID 的作用域是单张表：同一个 RID 必须与 TableId 一起使用才全局唯一。
struct RID {
    std::uint32_t page_id{0};
    std::uint16_t slot_id{0};

    auto operator<=>(const RID&) const = default;
};

} // namespace minidb

#endif // MINIDB_STORAGE_RID_H
