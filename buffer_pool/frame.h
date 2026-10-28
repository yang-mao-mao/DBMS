//
// Created by sjwan on 26-10-27.
//

#ifndef FRAME_H
#define FRAME_H

#pragma once

#include "../storage/ci_pan_cun_chu.h"   // Page, INVALID_PAGE

#include <cstdint>
using page_id_t = std::uint32_t;

struct Frame {
    Page          page{};                      // 页数据副本
    std::uint32_t page_id   = INVALID_PAGE;    // INVALID_PAGE = 空帧
    std::uint32_t pin_count = 0;               // 有多少人正使用这一帧  0 = 可淘汰
    bool          is_dirty  = false;           // 淘汰时是否需写回

    bool is_empty()  const { return page_id == INVALID_PAGE; }
    bool is_pinned() const { return pin_count > 0; }

    // 淘汰后调用：恢复成空帧（page 内容不擦，下次 fetch 会被 read_page 覆盖）
    void reset() {
        page_id   = INVALID_PAGE;
        pin_count = 0;
        is_dirty  = false;
    }
};


#endif //FRAME_H
