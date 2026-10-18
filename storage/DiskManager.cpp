#include "DiskManager.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t META_FORMAT_VERSION = PAGE_FORMAT_VERSION;

bool is_allocatable_type(PageType type) {
    return type == PageType::DATA
        || type == PageType::INDEX
        || type == PageType::OVERFLOW;
}

} // namespace

DiskManager::DiskManager(std::fstream&& file, const std::string& path)
    : file_(std::move(file)), path_(path) {}

bool DiskManager::write_meta(const Meta& meta) {
    Page page{};
    page.init(0, PageType::META);
    std::memcpy(page.body, &meta, sizeof(meta));
    page.update_checksum();
    return ::write_page(file_, 0, page);
}

namespace {

bool rollback_appended_page(std::fstream& file, const std::string& path,
                            std::uint64_t old_size) {
    file.clear();
    file.close();

    std::error_code error;
    std::filesystem::resize_file(path, old_size, error);
    if (error) {
        return false;
    }

    file.open(path, std::ios::binary | std::ios::in | std::ios::out);
    return file.is_open();
}

} // namespace

bool DiskManager::sync_meta() {
    return write_meta(meta_);
}

bool DiskManager::file_page_count(std::uint64_t& count) {
    if (!file_.is_open()) {
        return false;
    }

    file_.clear();
    file_.seekg(0, std::ios::end);
    if (!file_) {
        return false;
    }

    const std::streamoff size = file_.tellg();
    if (size < 0 || size % static_cast<std::streamoff>(PAGE_SIZE) != 0) {
        return false;
    }
    count = static_cast<std::uint64_t>(size)
        / static_cast<std::uint64_t>(PAGE_SIZE);
    return true;
}

bool DiskManager::init_new_file() {
    meta_ = Meta{};
    meta_.magic = PAGE_MAGIC;
    meta_.page_count = 1;
    meta_.freelist_head = INVALID_PAGE;
    meta_.file_version = META_FORMAT_VERSION;
    return sync_meta();
}

bool DiskManager::load_meta() {
    std::uint64_t actual_page_count = 0;
    if (!file_page_count(actual_page_count)
        || actual_page_count == 0
        || actual_page_count > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    Page page{};
    if (!::read_page(file_, 0, page)
        || page.header.page_type != PageType::META) {
        return false;
    }

    Meta loaded{};
    std::memcpy(&loaded, page.body, sizeof(loaded));
    if (loaded.magic != PAGE_MAGIC
        || loaded.file_version != META_FORMAT_VERSION
        || loaded.page_count > actual_page_count
        || loaded.page_count == 0
        || std::any_of(std::begin(loaded.reserved), std::end(loaded.reserved),
                       [](std::uint32_t value) { return value != 0; })) {
        return false;
    }

    meta_ = loaded;
    return reconcile_freelist(actual_page_count);
}

bool DiskManager::reconcile_freelist(std::uint64_t actual_page_count) {
    // Without a WAL, page type is the recovery source of truth. Rebuilding also
    // recovers FREE pages orphaned by a crash before the META freelist update.
    std::vector<std::uint32_t> free_pages;
    for (std::uint64_t id = meta_.page_count; id < actual_page_count; ++id) {
        Page page{};
        if (!::read_page(file_, id, page)
            || page.header.page_type != PageType::FREE) {
            return false;
        }
    }

    for (std::uint64_t id = 1; id < actual_page_count; ++id) {
        Page page{};
        if (!::read_page(file_, id, page)) {
            return false;
        }

        if (page.header.page_type == PageType::FREE) {
            if (page.header.slot_count != 0
                || page.header.free_start != sizeof(PageHeader)
                || page.header.free_end != PAGE_SIZE
                || page.header.fragment_bytes != 0
                || page.header.prev_page != INVALID_PAGE) {
                return false;
            }
            free_pages.push_back(static_cast<std::uint32_t>(id));
        } else if (!is_allocatable_type(page.header.page_type)) {
            return false;
        }
    }

    Meta recovered = meta_;
    recovered.page_count = static_cast<std::uint32_t>(actual_page_count);
    recovered.freelist_head = free_pages.empty() ? INVALID_PAGE : free_pages.front();

    for (std::size_t i = 0; i < free_pages.size(); ++i) {
        Page page{};
        if (!::read_page(file_, free_pages[i], page)) {
            return false;
        }
        const std::uint32_t next = i + 1 < free_pages.size()
            ? free_pages[i + 1]
            : INVALID_PAGE;
        if (page.header.next_page != next) {
            page.header.next_page = next;
            page.update_checksum();
            if (!::write_page(file_, free_pages[i], page)) {
                return false;
            }
        }
    }

    if (recovered.page_count != meta_.page_count
        || recovered.freelist_head != meta_.freelist_head) {
        if (!write_meta(recovered)) {
            return false;
        }
    }
    meta_ = recovered;
    free_pages_.clear();
    free_pages_.insert(free_pages.begin(), free_pages.end());
    return true;
}

std::unique_ptr<DiskManager> DiskManager::open(const std::string& path) {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        return nullptr;
    }

    if (!exists) {
        // app creates a missing file without truncating one created in a race.
        std::ofstream create(path,
                             std::ios::binary | std::ios::out | std::ios::app);
        if (!create.is_open()) {
            return nullptr;
        }
        create.close();
        if (!create) {
            return nullptr;
        }
    }

    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        return nullptr;
    }

    auto manager = std::unique_ptr<DiskManager>(
        new DiskManager(std::move(file), path));

    std::uint64_t actual_page_count = 0;
    if (!manager->file_page_count(actual_page_count)) {
        return nullptr;
    }
    if (actual_page_count == 0) {
        if (!manager->init_new_file()) {
            return nullptr;
        }
    } else if (!manager->load_meta()) {
        return nullptr;
    }

    return manager;
}

bool DiskManager::read_page(std::uint32_t page_no, Page& page) {
    if (!healthy_ || !is_valid_page(page_no)) {
        return false;
    }
    return ::read_page(file_, page_no, page);
}

bool DiskManager::write_page(std::uint32_t page_no, const Page& page) {
    if (!healthy_
        || !is_valid_page(page_no)
        || page_no == 0
        || free_pages_.contains(page_no)
        || page.header.page_type == PageType::META
        || page.header.page_type == PageType::FREE
        || page.header.page_id != page_no
        || !page.validate()) {
        return false;
    }

    if (!::write_page(file_, page_no, page)) {
        healthy_ = false;
        return false;
    }
    return true;
}

std::uint32_t DiskManager::allocate_page(PageType type) {
    if (!healthy_ || !is_allocatable_type(type)) {
        return INVALID_PAGE;
    }

    if (meta_.freelist_head != INVALID_PAGE) {
        const std::uint32_t page_no = meta_.freelist_head;
        Page free_page{};
        if (!read_page(page_no, free_page)
            || free_page.header.page_type != PageType::FREE) {
            return INVALID_PAGE;
        }

        Page allocated{};
        allocated.init(page_no, type);
        Meta candidate = meta_;
        candidate.freelist_head = free_page.header.next_page;
        if (!write_meta(candidate)) {
            if (!write_meta(meta_)) {
                healthy_ = false;
                file_.setstate(std::ios::badbit);
            }
            return INVALID_PAGE;
        }

        // Unlink first: a crash can leak this FREE page, but cannot leave a
        // non-FREE page reachable from the freelist.
        if (!::write_page(file_, page_no, allocated)) {
            const bool page_restored = ::write_page(file_, page_no, free_page);
            const bool meta_restored = write_meta(meta_);
            if (!page_restored || !meta_restored) {
                healthy_ = false;
                file_.setstate(std::ios::badbit);
            }
            return INVALID_PAGE;
        }

        meta_ = candidate;
        free_pages_.erase(page_no);
        return page_no;
    }

    if (meta_.page_count == INVALID_PAGE) {
        return INVALID_PAGE;
    }

    const std::uint32_t page_no = meta_.page_count;
    std::uint64_t actual_page_count = 0;
    if (!file_page_count(actual_page_count) || actual_page_count != page_no) {
        return INVALID_PAGE;
    }
    const std::uint64_t old_file_size =
        actual_page_count * static_cast<std::uint64_t>(PAGE_SIZE);

    Page free{};
    free.init(page_no, PageType::FREE);
    if (!::write_page(file_, page_no, free)) {
        if (!rollback_appended_page(file_, path_, old_file_size)) {
            healthy_ = false;
        }
        return INVALID_PAGE;
    }

    Meta candidate = meta_;
    ++candidate.page_count;
    candidate.freelist_head = page_no;
    if (!write_meta(candidate)) {
        const bool page_rolled_back =
            rollback_appended_page(file_, path_, old_file_size);
        if (!page_rolled_back || !write_meta(meta_)) {
            healthy_ = false;
            file_.setstate(std::ios::badbit);
        }
        return INVALID_PAGE;
    }
    meta_ = candidate;
    free_pages_.insert(page_no);
    return allocate_page(type);
}

bool DiskManager::free_page(std::uint32_t page_no) {
    if (page_no == 0 || !is_valid_page(page_no)) {
        return false;
    }

    Page original{};
    if (free_pages_.contains(page_no)
        || !read_page(page_no, original)
        || original.header.page_type == PageType::FREE
        || original.header.page_type == PageType::META) {
        return false;
    }

    Page free{};
    free.init(page_no, PageType::FREE);
    free.header.next_page = meta_.freelist_head;
    free.update_checksum();
    if (!::write_page(file_, page_no, free)) {
        if (!::write_page(file_, page_no, original)) {
            healthy_ = false;
            file_.setstate(std::ios::badbit);
        }
        return false;
    }

    Meta candidate = meta_;
    candidate.freelist_head = page_no;
    if (!write_meta(candidate)) {
        const bool page_restored = ::write_page(file_, page_no, original);
        const bool meta_restored = write_meta(meta_);
        if (!page_restored || !meta_restored) {
            healthy_ = false;
            file_.setstate(std::ios::badbit);
        }
        return false;
    }
    meta_ = candidate;
    free_pages_.insert(page_no);
    return true;
}

bool DiskManager::flush() {
    if (!healthy_ || !file_.is_open()) {
        return false;
    }
    file_.flush();
    if (!file_.good()) {
        healthy_ = false;
        return false;
    }
    return true;
}

DiskManager::~DiskManager() {
    if (file_.is_open()) {
        (void)flush();
        file_.close();
    }
}
