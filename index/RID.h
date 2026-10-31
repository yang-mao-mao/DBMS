#ifndef INDEX_RID_H
#define INDEX_RID_H

#include "../storage/ci_pan_cun_chu.h"

#include <cstdint>

struct RID {
    std::uint32_t page_id{INVALID_PAGE};
    std::uint16_t slot_id{0};

    [[nodiscard]] bool is_valid() const {
        return page_id != 0 && page_id != INVALID_PAGE;
    }

    bool operator==(const RID&) const = default;
};

#endif
