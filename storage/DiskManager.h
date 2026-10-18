#ifndef DISKMANAGER_H
#define DISKMANAGER_H

#include "ci_pan_cun_chu.h"
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>

class DiskManager {
public:
    // 不存在则创建并初始化；存在则打开并校验 META
    static std::unique_ptr<DiskManager> open(const std::string& path);

    ~DiskManager();

    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    // Reuses a free page first, otherwise appends one. Page 0 is reserved for META.
    std::uint32_t allocate_page(PageType type = PageType::DATA);

    // Releases a page and clears its old contents before it can be reused.
    bool free_page(std::uint32_t page_no);

    // 按页号读写：内部会 flush / 短读检测 / 校验和 / page_id 自描述校验
    bool read_page(std::uint32_t page_no, Page& page);
    bool write_page(std::uint32_t page_no, const Page& page);
    bool flush();

    std::uint32_t page_count() const { return meta_.page_count; }
    bool is_valid_page(std::uint32_t no) const { return no < meta_.page_count; }

private:
    DiskManager(std::fstream&& file, const std::string& path);

    struct Meta {
        std::uint32_t magic;
        std::uint32_t page_count;     // Includes page 0.
        std::uint32_t freelist_head;  // INVALID_PAGE when empty.
        std::uint32_t file_version;
        std::uint32_t reserved[4];
    };

    static_assert(std::is_trivially_copyable_v<Meta>);
    static_assert(sizeof(Meta) <= PAGE_SIZE - sizeof(PageHeader));

    bool init_new_file();          // 新文件：写 META
    bool load_meta();              // 旧文件：读并校验 META
    bool write_meta(const Meta& meta);
    bool sync_meta();
    bool reconcile_freelist(std::uint64_t actual_page_count);
    bool file_page_count(std::uint64_t& count);

    std::fstream file_;
    std::string  path_;
    bool healthy_{true};
    std::unordered_set<std::uint32_t> free_pages_;

    Meta meta_{};
};

#endif //DISKMANAGER_H
