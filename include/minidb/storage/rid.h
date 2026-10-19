#ifndef MINIDB_STORAGE_RID_H
#define MINIDB_STORAGE_RID_H

#include <compare>
#include <cstdint>

namespace minidb {

// The existing storage layer has no RID type. This table-scoped RID maps
// directly to its stable physical identifiers: PageHeader::page_id and Slot.
struct RID {
    std::uint32_t page_id{0};
    std::uint16_t slot_id{0};

    auto operator<=>(const RID&) const = default;
};

} // namespace minidb

#endif // MINIDB_STORAGE_RID_H
