//
// Created by sjwan on 26-10-27.

#include "BufferPoolManager.h"

#include <stdexcept>
#include <utility>

using namespace std;

//缓冲池的构建，当前架构：一个文件对应一个磁盘管理器对应一个缓冲池。
//缓冲池接收磁盘管理器，并且创建空白帧（数量决定缓冲池大小）
BufferPoolManager::BufferPoolManager(std::size_t pool_size, std::unique_ptr<DiskManager> disk_manager) :
     disk_manager_(std::move(disk_manager)),
     frames_(pool_size) {
    if (pool_size == 0 || !disk_manager_) {
        throw std::invalid_argument("BufferPoolManager: invalid arguments");
    }
    for (std::size_t i = 0; i < pool_size; ++i) {
        free_list_.push_back(i);
    }
}

BufferPoolManager::~BufferPoolManager() {
    // 修复：释放帧前尽力写回，避免正常析构丢失修改；需检查错误时应提前显式 flush_all_pages。
    (void)flush_all_pages();
}

std::optional<std::size_t> BufferPoolManager::find_available_frame() const {
    if (!free_list_.empty()) {
        return free_list_.front();
    }
    for (std::size_t index = 0; index < frames_.size(); ++index) {
        if (frames_[index].pin_count == 0) {
            return index;
        }
    }
    return std::nullopt;
}

//unpin_page()的功能，当某个人不再使用一页的时候，需要将pin_count减一
bool BufferPoolManager::unpin_page(page_id_t page_id, bool is_dirty) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;

    Frame& f = frames_[it->second];//把页号和帧号联系在一起的数据结构是page_table_
    if (f.pin_count == 0) return false;   // 没 pin 过，不能 unpin

    --f.pin_count;
    if (is_dirty) f.is_dirty = true;      // 只置 true，不置 false
    return true;
}

//这个函数的作用就是，根据页号，返回一个页。先检查缓冲池里有没有这一页，
//有就返回并且pin_count++(is_dirty==true说明磁盘上的是旧文件，内存是最新的版本)
//
Page* BufferPoolManager::fetch_page(page_id_t page_id) {
    // 修复：META 由 DiskManager 独占维护，缓存它会在分配/释放页后返回过期元数据。
    if (!disk_manager_ || page_id == INVALID_PAGE || page_id == 0
        || !disk_manager_->is_valid_page(page_id)) {
        return nullptr;
    }

    //命中,pin_count++，返回这一页
    if (auto it = page_table_.find(page_id); it != page_table_.end()) {
        Frame& frame = frames_[it->second];
        ++frame.pin_count;
        return &frame.page;
    }

    const auto frame_idx = find_available_frame();
    if (!frame_idx) {
        return nullptr;
    }

    // 修复：先读入临时页，读失败时不淘汰原页、不改脏标志，也不消耗空闲帧。
    Page loaded{};
    if (!disk_manager_->read_page(page_id, loaded)
        || loaded.header.page_type == PageType::META
        || loaded.header.page_type == PageType::FREE) {
        // 修复：FREE 页不能缓存，否则重用同一页号时 new_page 会误命中过期的 FREE 副本。
        return nullptr;
    }

    Frame& frame = frames_[*frame_idx];
    if (frame.page_id != INVALID_PAGE) {
        if (frame.is_dirty &&
            !disk_manager_->write_page(frame.page_id, frame.page)) {
            // 写回失败：不能丢弃脏数据，帧保持原状
            return nullptr;
        }
        page_table_.erase(frame.page_id);
    } else {
        free_list_.pop_front();
    }

    //维护frame（帧），page_table_（页号，帧号映射关系）
    frame.page = loaded;
    frame.page_id   = page_id;
    frame.pin_count = 1;
    frame.is_dirty  = false;
    page_table_[page_id] = *frame_idx;

    return &frame.page;

}

//flush_page / flush_all_pages	脏页落盘、清脏标志
//正常将脏页写回的时机有三个，
//一是当缓存未命中，查找空闲帧，发现没有空闲帧，但是有pin_count==0的帧，并且is_dirty==true的情况下，
//需要即刻写回磁盘；
//二是程序结束运行时，要将内存的缓冲池的所有脏页一次性写回磁盘。
//三是上层就是想将一个脏页写回磁盘，用于演示
//第一种情况已经在fetch_page中实现了，flush_all_page是在第二种情况下被调用

//实现逻辑：两个函数逻辑差不多。我的实现思路：
//接收一个页号，去page_tables中检查这一页是否位于缓冲池，
//若在，根据page_tables的值确定帧号，显式写回该帧；成功后清除脏标志。
bool BufferPoolManager::flush_page(page_id_t page_id) {
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) return false;

    Frame& f = frames_[it->second];
    // 修复：调用方可能仍持有 Page* 且尚未 unpin，显式刷盘不能仅依赖脏标志。
    if (!disk_manager_->write_page(page_id, f.page)) {
        f.is_dirty = true;                        // 写失败：保留重试机会，不能当作干净页淘汰
        return false;
    }
    f.is_dirty = false;                           // 只有写成功才清脏
    return true;
}
bool BufferPoolManager::flush_all_pages() {
    bool success = true;
    for (const auto& [page_id, frame_idx] : page_table_) {
        (void)frame_idx;
        // 修复：单页失败仍继续刷新其余页，避免一个坏页阻止其它修改落盘。
        if (!flush_page(page_id)) success = false;
    }
    return success;
}

//new_page() 先确认有可用帧，再分配磁盘页并加载到缓冲池。
Page* BufferPoolManager::new_page(page_id_t& page_id,
                                  PageType type) {
    // 修复：全帧被 pin 时直接失败，避免先分配再释放导致文件无意义增长、修改 freelist。
    if (!disk_manager_ || !find_available_frame()) return nullptr;

    // ① 让 DiskManager 分配页号：它会把新页 init 成 type 并落盘，
    //    返回的页号已经是“文件里存在、header 合法”的页。
    const std::uint32_t no = disk_manager_->allocate_page(type);
    if (no == INVALID_PAGE) return nullptr;

    // ② 把新页加载进缓冲池并 pin 住。
    //    fetch_page 未命中 → 从 free_list_ 取帧或淘汰 → read_page 读进来 → pin_count = 1
    Page* page = fetch_page(no);
    if (!page) {
        // 页已分配但读页或淘汰写回失败：回滚，把页还给 freelist。
        (void)disk_manager_->free_page(no);
        return nullptr;
    }

    // ③ 输出页号，返回已 pin 的页指针
    page_id = no;
    return page;
}


bool BufferPoolManager::delete_page(page_id_t page_id) {
    if (!disk_manager_ || page_id == INVALID_PAGE || page_id == 0) {
        return false;                       // META 页不可删
    }

    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        // ---------- 在池里 ----------
        Frame& frame = frames_[it->second];
        if (frame.pin_count > 0) {
            return false;                   // 有人正在用，不能删
        }

        // ① 先从磁盘释放（DiskManager 内部会处理 freelist / META）
        if (!disk_manager_->free_page(page_id)) {
            return false;                   // 失败：池里状态完全不变
        }

        // ② 再从池里摘帧（纯内存操作，不会失败）
        const std::size_t frame_idx = it->second;
        page_table_.erase(it);
        frame.reset();                      // page_id=INVALID, pin=0, dirty=false
        free_list_.push_back(frame_idx);
        return true;
    }

    // ---------- 不在池里 ----------
    return disk_manager_->free_page(page_id);
}

//
