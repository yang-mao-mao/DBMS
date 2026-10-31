//
// Created by sjwan on 26-10-27.
//

#ifndef BUFFERPOOLMANAGER_H
#define BUFFERPOOLMANAGER_H

#include "frame.h"
#include "../storage/DiskManager.h"
#include "../storage/ci_pan_cun_chu.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class BufferPoolManager {
public:
    BufferPoolManager(std::size_t pool_size,
                      std::unique_ptr<DiskManager> disk_manager);
    // 正常析构时尽力刷新；需要确认写入结果时应显式调用 flush_all_pages。
    ~BufferPoolManager();

    // 仅缓存已分配的 DATA/INDEX/OVERFLOW 页；成功返回的页增加一次 pin。
    Page* fetch_page(page_id_t page_id);
    bool unpin_page(page_id_t page_id, bool is_dirty);

    // 显式刷新包含仍被 pin 的页，不改变 pin 计数。
    bool flush_page(page_id_t page_id);
    bool flush_all_pages();

    // 失败时保持输出页号不变；成功返回的页需要由调用方 unpin。
    Page* new_page(page_id_t& page_id,
                   PageType type = PageType::DATA);
    bool delete_page(page_id_t page_id);

private:
    std::optional<std::size_t> find_available_frame() const;

    std::unique_ptr<DiskManager> disk_manager_;
    std::vector<Frame> frames_;
    std::unordered_map<page_id_t, std::size_t> page_table_;//键是页号，值是该页在frames中的下标
    std::deque<std::size_t> free_list_;//空闲帧队列，存储的是帧号
};




#endif //BUFFERPOOLMANAGER_H
